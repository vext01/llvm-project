// FIXME: make this file look like other LLVM passes. Comments etc.

#ifndef LLVM_TRANSFORMS_YKTRACEINPUTS_YKTRACEINPUTS_H
#define LLVM_TRANSFORMS_YKTRACEINPUTS_YKTRACEINPUTS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
  class YkTraceInputsPass : public PassInfoMixin<YkTraceInputsPass> {
    //void injectInputs(CallInst *StartInst, CallInst *StopInst);

    public:
      PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  };
}

#endif
