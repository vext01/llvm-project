//===- ControlPoint.cpp - Synthesise the yk control point -----------------===//
//
// This pass finds the user's call to the dummy control point and replaces it
// with a call to a new control point that implements the necessary logic to
// drive the yk JIT.
//
// The pass converts an interpreter loop that looks like this:
//
// ```
// YkMT *mt = ...;
// YkLocation loc = ...;
// pc = 0;
// while (...) {
//     yk_mt_control_point(mt, loc); // <- dummy control point
//     bc = program[pc];
//     switch (bc) {
//         // bytecode handlers here.
//     }
// }
// ```
//
// Into one that looks like this (note that this transformation happens at the
// IR level):
//
// ```
// // The YkCtrlPointStruct contains one member for each live LLVM variable
// // just before the call to the control point.
// struct YkCtrlPointStruct {
//     size_t pc;
// }
//
// struct YkCtrlPointStruct cp_vars;
// pc = 0;
// while (...) {
//     // Now we call the patched control point.
//     cp_vars.pc = pc;
//     __ykrt__control_point(mt, loc, &cp_vars);
//     pc = cp_vars.pc;
//     bc = program[pc];
//     switch (bc) {
//         // bytecode handlers here.
//     }
// }
// ```
//
// Note that this transformation occurs at the LLVM IR level. The above example
// is shown as C code for easy comprehension.

#include "llvm/Transforms/Yk/ControlPoint.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "yk-control-point"
#define JIT_STATE_PREFIX "jit-state: "

#define YK_OLD_CONTROL_POINT_NUM_ARGS 2

#define YK_CONTROL_POINT_ARG_MT_IDX 0
#define YK_CONTROL_POINT_ARG_LOC_IDX 1
#define YK_CONTROL_POINT_ARG_VARS_IDX 2
#define YK_CONTROL_POINT_NUM_ARGS 3

using namespace llvm;

/// Find the call to the dummy control point that we want to patch.
/// Returns either a pointer the call instruction, or `nullptr` if the call
/// could not be found.
/// YKFIXME: For now assumes there's only one control point.
CallInst *findControlPointCall(Module &M) {
  // Find the declaration of `yk_mt_control_point()`.
  Function *CtrlPoint = M.getFunction(YK_DUMMY_CONTROL_POINT);
  if (CtrlPoint == nullptr)
    return nullptr;

  // Find the call site of `yk_mt_control_point()`.
  Value::user_iterator U = CtrlPoint->user_begin();
  if (U == CtrlPoint->user_end())
    return nullptr;

  return cast<CallInst>(*U);
}

/// Wrapper to make `std::set_difference` more concise.
///
/// Store the difference between `S1` and `S2` into `Into`.
void vset_difference(const std::set<Value *> &S1, const std::set<Value *> &S2,
                     std::set<Value *> &Into) {
  std::set_difference(S1.begin(), S1.end(), S2.begin(), S2.end(),
                      std::inserter(Into, Into.begin()));
}

/// Wrapper to make `std::set_union` more concise.
///
/// Store the union of `S1` and `S2` into `Into`.
void vset_union(const std::set<Value *> &S1, const std::set<Value *> &S2,
                std::set<Value *> &Into) {
  std::set_union(S1.begin(), S1.end(), S2.begin(), S2.end(),
                 std::inserter(Into, Into.begin()));
}

namespace llvm {
// A liveness analysis for LLVM IR.
//
// This is based on the algorithm shown in Chapter 10 of the book:
//
//   Modern Compiler Implementation in Java (2nd edition)
//   by Andrew W. Appel
class LivenessAnalysis {
  std::map<Instruction *, std::set<Value *>> In;

  /// Find the successor instructions of the specified instruction.
  std::set<Instruction *> getSuccessorInstructions(Instruction *I) {
    Instruction *Term = I->getParent()->getTerminator();
    std::set<Instruction *> SuccInsts;
    if (I != Term) {
      // Non-terminating instruction: the sole successor instruction is the
      // next instruction in the block.
      SuccInsts.insert(I->getNextNode());
    } else {
      // Terminating instruction: successor instructions are the first
      // instructions of all successor blocks.
      for (unsigned SuccIdx = 0; SuccIdx < Term->getNumSuccessors(); SuccIdx++)
        SuccInsts.insert(&*Term->getSuccessor(SuccIdx)->begin());
    }
    return SuccInsts;
  }

  /// Replaces the value set behind the pointer `S` with the value set `R` and
  /// returns whether the set behind `S` changed.
  bool updateValueSet(std::set<Value *> *S, const std::set<Value *> R) {
    bool Changed = (*S != R);
    *S = R;
    return Changed;
  }

public:
  LivenessAnalysis(Function *Func) {
    if (Func->empty())
      return;

    // Compute defs and uses for each instruction.
    std::map<Instruction *, std::set<Value *>> Defs;
    std::map<Instruction *, std::set<Value *>> Uses;
    for (BasicBlock &BB : *Func) {
      for (Instruction &I : BB) {
        // Record what this instruction defines.
        if (!I.getType()->isVoidTy())
          Defs[&I].insert(cast<Value>(&I));

        // Record what this instruction uses.
        //
        // Note that Phi nodes are special and must be skipped. If we consider
        // their operands as uses, then Phi nodes in loops may use variables
        // before they are defined, and this messes with the algorithm.
        //
        // The book doesn't cover this quirk, as it explains liveness for
        // non-SSA form, and thus doesn't need to worry about Phi nodes.
        if (isa<PHINode>(I))
          continue;

        for (auto *U = I.op_begin(); U < I.op_end(); U++) {
          if ((!isa<Constant>(U)) && (!isa<BasicBlock>(U)) &&
              (!isa<MetadataAsValue>(U)) && (!isa<InlineAsm>(U))) {
            Uses[&I].insert(*U);
          }
        }
      }
    }

    // A function implicitly defines its arguments.
    //
    // To propagate the arguments properly we pretend that the first instruction
    // in the entry block defines the arguments.
    Instruction *FirstInst = &*Func->getEntryBlock().begin();
    for (auto &Arg : Func->args())
      Defs[FirstInst].insert(&Arg);

    // Compute the live sets for each instruction.
    //
    // This is the fixed-point of the following data-flow equations (page 206
    // in the book referenced above):
    //
    //    in[I] = use[I] ∪ (out[I] - def[I])
    //
    //    out[I] =       ∪
    //             (S in succ[I])    in[S]
    //
    // Note that only the `In` map is kept after this constructor ends, so
    // only `In` is a field.
    std::map<Instruction *, std::set<Value *>> Out;
    bool Changed;
    do {
      Changed = false;
      // As the book explains, fixed-points are reached quicker if we process
      // control flow in "approximately reverse direction" and if we compute
      // `out[I]` before `in[I]`.
      //
      // Because the alrogithm works by propagating liveness from use sites
      // backwards to def sites (where liveness is killed), by working
      // backwards we are able to propagate long runs of liveness in one
      // iteration of the algorithm.
      for (BasicBlock *BB : post_order(&*Func)) {
        for (BasicBlock::reverse_iterator II = BB->rbegin(); II != BB->rend();
             II++) {
          Instruction *I = &*II;
          // Update out[I].
          std::set<Instruction *> SuccInsts = getSuccessorInstructions(I);
          std::set<Value *> NewOut;
          for (Instruction *SI : SuccInsts) {
            NewOut.insert(In[SI].begin(), In[SI].end());
          }
          Changed |= updateValueSet(&Out[I], std::move(NewOut));

          // Update in[I].
          std::set<Value *> OutMinusDef;
          vset_difference(Out[I], Defs[I], OutMinusDef);

          std::set<Value *> NewIn;
          vset_union(Uses[I], OutMinusDef, NewIn);
          Changed |= updateValueSet(&In[I], std::move(NewIn));
        }
      }
    } while (Changed); // Until a fixed-point.
  }

  /// Returns the set of live variables immediately before the specified
  /// instruction.
  std::set<Value *> getLiveVarsBefore(Instruction *I) { return In[I]; }
};

void initializeYkControlPointPass(PassRegistry &);
} // namespace llvm

namespace {
class YkControlPoint : public ModulePass {
public:
  static char ID;
  YkControlPoint() : ModulePass(ID) {
    initializeYkControlPointPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    LLVMContext &Context = M.getContext();

    Intrinsic::ID SMFuncID = Function::lookupIntrinsicID("llvm.experimental.stackmap");
    if (SMFuncID == Intrinsic::not_intrinsic) {
      Context.emitError("can't find stackmap()");
      return false;
    }
    Function *SMFunc = Intrinsic::getDeclaration(&M, SMFuncID);

    std::map<Instruction *, std::set<Value *>> SMCalls;
    for (Function &F: M) {
      LivenessAnalysis LA(&F);
      for (BasicBlock &BB: F) {
        for (Instruction &I: BB) {
          if ((isa<CallInst>(I)) || ((isa<BranchInst>(I)) && (cast<BranchInst>(I).isConditional())) || isa<SwitchInst>(I)) {
            SMCalls.insert({&I, LA.getLiveVarsBefore(&I)});
          }
        }
      }
    }

    uint64_t Count = 0;
    uint64_t LiveCount = 0;
    Value *Shadow = ConstantInt::get(Type::getInt32Ty(Context), 0);
    for (auto It: SMCalls) {
      Instruction *I = cast<Instruction>(It.first);
      std::set<Value *> L = It.second;

      IRBuilder<> Bldr(I);
      Value *SMID = ConstantInt::get(Type::getInt64Ty(Context), Count);
      std::vector<Value *> Args = {SMID, Shadow};
      for (Value *A: L) {
        Type *TheTy = A->getType();
        if ((TheTy != Type::getInt8Ty(Context)) &&
          (TheTy != Type::getInt16Ty(Context)) &&
          (TheTy != Type::getInt32Ty(Context)) &&
          (TheTy != Type::getInt64Ty(Context))) {
            continue;
        }
        Args.push_back(A);
        LiveCount++;
      }
      assert(SMFunc != nullptr);
      Bldr.CreateCall(SMFunc->getFunctionType(), SMFunc, ArrayRef<Value *>(Args));
      Count++;
    }

    errs() << "Injected " << Count << " stackmaps and " << LiveCount << "Live vars\n";

//#ifndef NDEBUG
    // Our pass runs after LLVM normally does its verify pass. In debug builds
    // we run it again to check that our pass is generating valid IR.
    //M.dump();
    if (verifyModule(M, &errs())) {
      Context.emitError("Control point pass generated invalid IR!");
      return false;
    }
//#endif
    return true;
  }
};
} // namespace

char YkControlPoint::ID = 0;
INITIALIZE_PASS(YkControlPoint, DEBUG_TYPE, "yk control point", false, false)

ModulePass *llvm::createYkControlPointPass() { return new YkControlPoint(); }
