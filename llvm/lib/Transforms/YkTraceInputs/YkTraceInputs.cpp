#include "llvm/Transforms/YkTraceInputs/YkTraceInputs.h"
#include "llvm/Support/Debug.h"
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

    // FIXME write tests for these assertions.

    // Check that to get to `StopInst`, you must have first executed `StartInst`.
    DominatorTree DT(*Caller);
    assert(DT.dominates(StartInst, StopInst));

    // Check that there's no way to execute `StartInst` without executing
    // `StopInst`.
    PostDominatorTree PDT(*Caller);
    assert(PDT.dominates(StopInst, StartInst));

    StartInst->dump();
    StopInst->dump();
  }
  return PreservedAnalyses::all();
}
