#include "llvm/Transforms/YkTraceInputs/YkTraceInputs.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/PostDominators.h"

#define DEBUG_TYPE "yk-trace-inputs"

using namespace llvm;

//void YkTraceInputsPass::injectInputs(CallInst *StartInst, CallInst *StopFunc) {
//  //errs() << ">>> " << F->getName() << "\n";
//}
//

bool ValidTraceInput(Value *V) {
  if ((isa<Constant>(V)) || (isa<MetadataAsValue>(V)) || (isa<BasicBlock>(V)))
    return false;
  return true;
}

PreservedAnalyses YkTraceInputsPass::run(Module &M, ModuleAnalysisManager &AM) {
  Function *StartTracingFunc = M.getFunction("__yktrace_start_tracing");
  if (StartTracingFunc == nullptr)
    return PreservedAnalyses::all();

  Function *StopTracingFunc = M.getFunction("__yktrace_stop_tracing");
  assert(StopTracingFunc != nullptr);

  for (User *U: StartTracingFunc->users()) {
    if (!isa<CallInst>(U))
      continue;

    // Identify the traced section of code.
    CallInst *StartInst = cast<CallInst>(U);

    // FIXME proper error for this.
    assert(StartInst->arg_size() == 2); // tracing kind, num_inputs.

    Function *Caller = StartInst->getFunction();
    // FIXME find the StopInst as we traverse the CFG below, rather than by
    // pre-scanning like this.
    CallInst *StopInst = nullptr;
    for (auto &BB: *Caller) {
      for (auto &I: BB) {
        if (isa<CallInst>(&I)) {
            CallInst *CI = cast<CallInst>(&I);
            Function *CF = CI->getCalledFunction();
            if (CF != StopTracingFunc)
              continue;
            // We assume that any given function only has one traced section.
            // FIXME early return if not built with assertions.
            assert(StopInst == nullptr);
            StopInst = CI;
        }
      }
    }
    assert(StopInst != nullptr);

    // We can't check these, due to indirect branches.
    // FIXME: write tests for these assertions?
    // Check that to get to `StopInst`, you must have first executed `StartInst`.
    //DominatorTree DT(*Caller);
    //assert(DT.dominates(StartInst, StopInst));

    // Check that there's no way to execute `StartInst` without executing
    // `StopInst`.
    //PostDominatorTree PDT(*Caller);
    //assert(PDT.dominates(StopInst, StartInst));

    // Walk the CFG looking for inputs.
    bool FirstBlock = true;
    std::vector<BasicBlock *> Work;
    std::set<Value *> DefinedInTrace;
    std::set<Value *> NewOperands;
    std::set<BasicBlock *> SeenBlocks;
    Work.push_back(StartInst->getParent());
    while (!Work.empty()) {
      BasicBlock *BB = Work.back();
      SeenBlocks.insert(BB);
      Work.pop_back();
      errs() << "Work: ";
      BB->dump();
      bool ProcessSuccs = true;
      for (auto I = BB->begin(); I != BB->end(); I++) {
        if (FirstBlock) {
          // Skip to the instruction immediately after `StartInst`.
          while (&*I != StartInst) {
            I++;
            assert(I != BB->end());
          }
          FirstBlock = false;
          continue;
        }

        // If we see `StopInst` then don't traverse further on this path.
        if (&*I == StopInst) {
          ProcessSuccs = false;
          // FIXME move finding of StopInst in here somewhere (and the
          // dominator checks).
          break;
        }

        // If we get here then `I` is an instruction inside the traced section.
        // We must ensure that each of its operands are either: also defined
        // within the traced section; or passed in as a trace input (as an
        // argument to `StartInst`).
        if (!I->getType()->isVoidTy()) { // i.e. it defines a value.
          DefinedInTrace.insert(&*I);
        }

        // FIXME: can/should we omit the thread tracer from trace inputs?
        //errs() << ">> ";
        //I->dump();
        if (isa<CallInst>(I)) {
          // Special case for calls to prevent the callee operand being
          // classified as a trace input.
          //errs() << "CallInst\n";
          for (auto &O: cast<CallInst>(I)->args()) {
            //O->dump();
            if ((ValidTraceInput(&*O)) && (!DefinedInTrace.count(&*O)))
              NewOperands.insert(O);
          }
        } else if (isa<PHINode>(I)) {
          // The operands of a PHI node would confuse us, as they are not
          // actual uses.
          // FIXME this isn't right.
          continue;
        } else {
          //errs() << "Normal\n";
          for (auto &O: I->operands()) {
            //O->dump();
            if ((ValidTraceInput(O)) && (!DefinedInTrace.count(&*O)))
              NewOperands.insert(O);
          }
        }
      }

      if (ProcessSuccs) {
        Instruction *Term = BB->getTerminator();
        for (auto Succ: successors(Term)) {
          if (!SeenBlocks.count(Succ)) // Avoid CFG cycles.
            Work.push_back(&*Succ);
        }
      }
    }

    // Now we know all of the trace inputs, we construct a new call to and
    // replace the old one.
    std::vector<Value *> NewOperandsVec;
    NewOperandsVec.push_back(StartInst->getArgOperand(0)); // Add the TracingKind arg.
    NewOperandsVec.push_back(ConstantInt::get(StartInst->getArgOperand(1)->getType(), NewOperands.size())); // Add the number of inputs.
    for (Value *V: NewOperands) {
      NewOperandsVec.push_back(V);
    }
    CallInst *NewCall = CallInst::Create(StartTracingFunc->getFunctionType(), StartTracingFunc, NewOperandsVec, "", StartInst);
    StartInst->replaceAllUsesWith(NewCall);
    StartInst->eraseFromParent();
  }

  M.dump();

  return PreservedAnalyses::all();
}
