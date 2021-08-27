#include "llvm/Transforms/YkTraceInputs/YkTraceInputs.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "yk-trace-inputs"

using namespace llvm;

PreservedAnalyses YkTraceInputsPass::run(Function &F, FunctionAnalysisManager &AM) {
  errs() << "YkTraceInputsPass::run(): " << F.getName() << "\n";
  return PreservedAnalyses::all();
}
