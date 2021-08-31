#include "llvm/Transforms/YkTraceInputs/YkTraceInputs.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"

#define DEBUG_TYPE "yk-trace-inputs"

using namespace llvm;

PreservedAnalyses YkTraceInputsPass::run(Module &M, ModuleAnalysisManager &AM) {
  errs() << "My module pass\n";

  Function *StartTracingFunc = M.getFunction("__yktrace_start_tracing");
  if (StartTracingFunc == nullptr)
    return PreservedAnalyses::all();

  errs() << "Used by:\n";
  for (User *U: StartTracingFunc->users()) {
    if (!isa<CallInst>(U))
      continue;
    U->dump();
  }
  return PreservedAnalyses::all();
}
