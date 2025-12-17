//===- SplitBlocksAfterCalls.cpp -===//
//
// Makes function calls effectively terminators by splitting blocks after each
// call. This ensures that there can only be at most one call per block. This
// is used in order to detect recursion and external function calls within a
// trace.

#include "llvm/Transforms/Yk/SplitBlocksAfterCalls.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Yk/ControlPoint.h"
#include "llvm/YkIR/YkIRWriter.h"

#include <set>

#define DEBUG_TYPE "yk-splitblocksaftercalls"

using namespace llvm;

namespace llvm {
void initializeYkSplitBlocksAfterCallsPass(PassRegistry &);
} // namespace llvm

namespace {

class YkSplitBlocksAfterCalls : public ModulePass {
public:
  static char ID;
  YkSplitBlocksAfterCalls() : ModulePass(ID) {
    initializeYkSplitBlocksAfterCallsPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    AU.addRequired<llvm::CallGraphWrapperPass>();
    AU.setPreservesAll(); // if you don't change the call graph
  }

  bool couldExecuteTraceableCode(CallGraphNode *Node, std::map<Function *, bool> &Cache) {
    std::set<Function *> SeenFuncs;
    return couldExecuteTraceableCode(Node, Cache, SeenFuncs);
  }

  bool couldExecuteTraceableCode(CallGraphNode *Node, std::map<Function *, bool> &Cache, std::set<Function *> &SeenFuncs) {
    Function *F = Node->getFunction();
    assert(Node);
    // Break cycles caused by recursion.
    if (SeenFuncs.count(F) != 0) {
      return false;
    }
    SeenFuncs.insert(F);
    assert(Node != nullptr);

    // errs() << "couldCallBack: " << Node->getFunction()->getName() << "\n";
    if (Cache.count(Node->getFunction()) != 0) {
      // cache hit
      // errs() << "cache hit: " << Cache[Node->getFunction()] << "\n";
      return Cache[Node->getFunction()];
    }

    // cache miss
    // errs() << "cache miss\n";
    bool Result = false;
    if (F->isDeclaration()) {
      // We assume that intrinsics can not call back, but that all other
      // declarations could.
      // errs() << "DECL COULD CALLBACK: " << F->getName() << " " << !F->isIntrinsic() << "\n";
      Result = !F->isIntrinsic();
    } else if (!F->hasFnAttribute(YK_OUTLINE_FNATTR)) {
      // A function not marked yk_outline is itself traceable.
      Result = true;
    } else if(containsControlPoint(*F)) {
      // The control point is an exception in that it *is* marked `yk_outline`,
      // but it is in fact traceable.
      Result = true;
    } else {
      // Otherwise we have to see if the function's callees could execute
      // traceable code.
      //
      // An iterative implementation would be better, but in reality, the
      // bottom-up walk of the call-graph will limit minimise the recursion
      // depth (callees will often already be in the cache).
      for (auto &CallRecord : *Node) {
        llvm::CallGraphNode *CalleeNode = CallRecord.second;
        if (!CallRecord.first) {
          // errs() << "no inst\n";
          // No call instruction associated with this edge. It's an implicit
          // relationship that doesn't concern us.
          continue;
        }
        // (*CallRecord.first)->dump();
        CallBase *Call = cast<CallBase>(*CallRecord.first);
        // Call->dump();
        Function *CF = Call->getCalledFunction();
        if (!CF || !CalleeNode->getFunction()) {
          // The callee is special or isn't known. Conservatively assume that the
          // caller could call back.
          // errs() << "special\n";
          Result = true;
          break;
        }
        // errs() << Node->getFunction()->getName() << " calls: " << CalleeNode->getFunction()->getName() << "\n";
        if (couldExecuteTraceableCode(CalleeNode, Cache, SeenFuncs)) {
          Result = true;
          break;
        }
      }
    }

    // errs() << "cache insert: " << Node->getFunction()->getName() << " = " << Result << "\n";
    Cache.insert({Node->getFunction(), Result});
    if (!Result) {
      Node->getFunction()->addFnAttr("yk_no_callback");
    }
    return Result;
  }

  bool runOnModule(Module &M) override {
    // M.dump();

    LLVMContext &Context = M.getContext();

    // Walk the module annotating those which definitely can't call back to
    // traceable code.
    //
    // We do this bottom-up to be more efficient.
    auto &CG = getAnalysis<llvm::CallGraphWrapperPass>().getCallGraph();
    std::map<Function *, bool> Cache;
    for (llvm::scc_iterator<llvm::CallGraph*> I = scc_begin(&CG),
                                               E = scc_end(&CG);
         I != E; ++I) {
      const std::vector<llvm::CallGraphNode*> &SCC = *I;

      // Walk functions of the SCC.
      for (llvm::CallGraphNode *Node : SCC) {
        llvm::Function *F = Node->getFunction();
        if (!F) {
          continue;
        }
        errs() << "TOP: " << F->getName() << "\n";
        bool Res = couldExecuteTraceableCode(Node, Cache);
        errs() << "/TOP: " << F->getName() << " " << Res << "\n";
      }
    }

    for (Function &F : M) {
      if (F.empty()) // skip declarations.
        continue;

      // If we won't trace this function, no need for this transformation.
      if ((F.hasFnAttribute(YK_OUTLINE_FNATTR)) && (!containsControlPoint(F))) {
        continue;
      }

      // As we will be modifying the blocks of this function inplace, we
      // require a work list to process all existing and newly inserted blocks
      // in order to not miss any.
      std::vector<BasicBlock *> Todo;
      std::set<BasicBlock *> Seen;
      BasicBlock &Entry = F.getEntryBlock();

      // This pass requires the `NoCallsInEntryBlocksPass` to have run first,
      // which in turn needs to run before the shadowstack pass. Otherwise,
      // this pass would split the block after the shadowstack malloc, which
      // results in allocas outside of the entry block which breaks stackmaps.
      Instruction *T = Entry.getTerminator();
      for (size_t I = 0; I < T->getNumSuccessors(); I++) {
        Todo.push_back(T->getSuccessor(I));
      }
      Seen.insert(&Entry);
      while (!Todo.empty()) {
        BasicBlock *Next = Todo.back();
        Todo.pop_back();
        if (Seen.count(Next) > 0) {
          continue;
        }
        Seen.insert(Next);

        for (Instruction &I : *Next) {
          if (I.isDebugOrPseudoInst()) {
            continue;
          }
          if (isa<CallInst>(I)) {
            // YKFIXME: Can we determine at compile time if inline asm contains
            // calls or jumps, e.g. via `getAsmString`, and then not split the
            // block after them?
            CallInst *CI = cast<CallInst>(&I);
            Function *CF = CI->getCalledFunction();
            if (CF && CF->getName() == "llvm.frameaddress.p0") {
              // This call is always inlined so we don't need to split the
              // block here.
              //
              // FIXME: can we kill this?
              continue;
            }
            // If the next instruction is an unconditional branch, we don't
            // need to split the block.
            if (BranchInst *BI = dyn_cast<BranchInst>(I.getNextNode())) {
              if (BI->isUnconditional()) {
                continue;
              }
            }

            // For now we assume that inline asm can't call back.
            // FIXME: it could.
            if (isa<InlineAsm>(CI->getCalledOperand())) {
              continue;
            }

            // If the callee can't call back, then no need to split.
            //
            // If `CF` is null, then it's likely an indirect call, so we will
            // have to conservatively split the block.
            //
            // We also have to split at the control point.
            if (!CF || Cache.at(CF) || CF->getName().startswith("llvm.experimental.patchpoint.")) {
            //if (!CF || Cache.at(CF)) {
              // errs() << "YES block split in " << F.getName() << " for: "; CI->dump();
            } else {
              // errs() << "NO block split in " << F.getName() << " for: "; CI->dump();
              continue;
            }

            // Since `splitBasicBlock` splits before the given instruction,
            // pass the instruction following this call instead.
            Next->splitBasicBlock(I.getNextNode());
            break;
          }
        }

        // Add successors to todo list.
        Instruction *T = Next->getTerminator();
        for (size_t I = 0; I < T->getNumSuccessors(); I++) {
          Todo.insert(Todo.begin(), T->getSuccessor(I));
        }
      }
    }

    // M.dump();

#ifndef NDEBUG
    // Our pass runs after LLVM normally does its verify pass. In debug builds
    // we run it again to check that our pass is generating valid IR.
    if (verifyModule(M, &errs())) {
      Context.emitError("Stackmap insertion pass generated invalid IR!");
      return false;
    }
#endif
    return true;
  }
};
} // namespace

char YkSplitBlocksAfterCalls::ID = 0;
INITIALIZE_PASS(YkSplitBlocksAfterCalls, DEBUG_TYPE,
    "yk split blocks after calls", false, false)

ModulePass *llvm::createYkSplitBlocksAfterCallsPass() {
  return new YkSplitBlocksAfterCalls();
}
