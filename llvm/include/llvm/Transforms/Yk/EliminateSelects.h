#ifndef LLVM_TRANSFORMS_YK_ELIMINATESELECTS_H
#define LLVM_TRANSFORMS_YK_ELIMINATESELECTS_H

#include "llvm/Pass.h"

namespace llvm {
ModulePass *createYkEliminateSelectsPass();
} // namespace llvm

#endif
