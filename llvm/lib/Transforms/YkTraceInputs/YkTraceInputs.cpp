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
            errs() << "Num: " << CI->getNumOperands() << "\n";
            assert(CI->arg_size() == 1); // Only the tracing kind arg.
            StopInst = CI;
        }
      }
    }
    assert(StopInst != nullptr);

    errs() << "Trace between:\n";
    StartInst->dump();
    StopInst->dump();

    errs() << "In function:\n";
    Caller->dump();

    // FIXME write tests for these assertions.

    // Check that to get to `StopInst`, you must have first executed `StartInst`.
    DominatorTree DT(*Caller);
    assert(DT.dominates(StartInst, StopInst));

    // Check that there's no way to execute `StartInst` without executing
    // `StopInst`.
    PostDominatorTree PDT(*Caller);
    assert(PDT.dominates(StopInst, StartInst));

    // Walk the CFG looking for inputs.
    errs() << "Running:\n";
    bool FirstBlock = true;
    std::vector<BasicBlock *> Work;
    std::set<Value *> DefinedInTrace;
    std::set<Value *> NewOperands;
    Work.push_back(StartInst->getParent());
    while (!Work.empty()) {
      BasicBlock *BB = Work.back();
      Work.pop_back();
      errs() << "New Block\n";
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

        errs() << "Instr:\n";
        I->dump();

        // If we get here then `I` is an instruction inside the traced section.
        // We must ensure that each of its operands are either: also defined
        // within the traced section; or passed in as a trace input (as an
        // argument to `StartInst`).
        if (!I->getType()->isVoidTy()) { // i.e. it defines a value.
          DefinedInTrace.insert(&*I);
        }

        for (auto &O: I->operands()) {
          if (!DefinedInTrace.count(&*O)) {
            // This operand isn't defined in the trace and needs to be a trace input.
            errs() << "Not defined in trace: \n";
            O->dump();
            // FIXME: How do I add the argument to StartInst?
            //StartInst->setArgOperand(NextPatchedArgIdx++, O);
            //StartInst->getOperandList()[NextPatchedArgIdx++] = O;
            //StartInst->getOperandList()->insert(O, StartInst->operands_end());
            NewOperands.insert(O);
          }
        }
      }

      if (ProcessSuccs) {
        Instruction *Term = BB->getTerminator();
        for (auto Succ: successors(Term)) {
          errs() << "Add Work\n";
          Work.push_back(&*Succ);
        }
      }
    }

    // Now we know all of the trace inputs, we construct a new call to and
    // replace the old one.
    errs() << "XXX: " << StartTracingFunc->getFunctionType()->getNumParams() << "\n";
    errs() << "YYY: " << NewOperands.size() << "\n";
    std::vector<Value *> NewOperandsVec;
    NewOperandsVec.push_back(StartInst->getArgOperand(0)); // Add the TracingKind arg.
    for (Value *V: NewOperands) {
      NewOperandsVec.push_back(V);
    }
    StartTracingFunc->dump();
    CallInst *NewCall = CallInst::Create(StartTracingFunc->getFunctionType(), StartTracingFunc, NewOperandsVec, "", StartInst);
    NewCall->dump();
    StartInst->replaceAllUsesWith(NewCall);
    StartInst->eraseFromParent();
  }
  M.dump();
  return PreservedAnalyses::all();
}
