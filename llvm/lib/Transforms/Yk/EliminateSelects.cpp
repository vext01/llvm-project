//===- EliminateSelects.cpp - XXX -==
//
// XXX

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Yk/EliminateSelects.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "yk-eliminate-selects"

using namespace llvm;

namespace llvm {
void initializeYkEliminateSelectsPass(PassRegistry &);
} // namespace llvm

namespace {
class YkEliminateSelects : public ModulePass {
public:
  static char ID;
  YkEliminateSelects() : ModulePass(ID) {
    initializeYkEliminateSelectsPass(*PassRegistry::getPassRegistry());
  }

  void eliminateSelect(SelectInst *I) {
    LLVMContext &Ctxt = I->getContext();
    Value *Cond = I->getCondition();
    Value *TrueVal = I->getTrueValue();
    Value *FalseVal = I->getFalseValue();

    // First find/create all of the blocks that we will need.
    BasicBlock *OldBB = I->getParent();
    BasicBlock *TrueBB = BasicBlock::Create(Ctxt, "", I->getFunction());
    BasicBlock *FalseBB = BasicBlock::Create(Ctxt, "", I->getFunction());
    BasicBlock *MergeBB = llvm::SplitBlock(OldBB, I);

    IRBuilder Builder(Ctxt);

    // Replace the `select` with a conditional branch.
    Instruction *OldTerm = OldBB->getTerminator();
    assert(OldTerm != nullptr);
    assert(isa<BranchInst>(OldTerm));
    assert(!cast<BranchInst>(OldTerm)->isConditional());
    OldTerm->eraseFromParent();
    Builder.SetInsertPoint(OldBB);
    Builder.CreateCondBr(Cond, TrueBB, FalseBB);

    // The true/false branches are then empty blocks going to the merge block.
    Builder.SetInsertPoint(TrueBB);
    Builder.CreateBr(MergeBB);
    Builder.SetInsertPoint(FalseBB);
    Builder.CreateBr(MergeBB);

    // The merge block uses a PHI node to choose the right value.
    Builder.SetInsertPoint(&MergeBB->front());
    PHINode *PHI = Builder.CreatePHI(I->getType(), 0);
    PHI->addIncoming(TrueVal, TrueBB);
    PHI->addIncoming(FalseVal, FalseBB);

    // Remove the old select instruction and update all uses of the result.
    I->replaceAllUsesWith(PHI);
    I->eraseFromParent();

    // errs() << "Select replaced!\n";
  }

  bool runOnModule(Module &M) override {
    // errs() << "BEFORE:\n";
    // M.dump();

    bool Changed = false;
    std::vector<SelectInst *> Selects;
    for (Function &F : M) {
      for (BasicBlock &BB: F) {
        for (Instruction &I: BB) {
          if (SelectInst *SI = dyn_cast<SelectInst>(&I)) {
            Selects.push_back(SI);
          }
        }
      }
    }

    for (SelectInst *SI: Selects) {
      eliminateSelect(SI);
      Changed = true; // XXX do once
    }

    // errs() << "After:\n";
    // M.dump();

    return Changed;
  }
};
} // namespace

char YkEliminateSelects::ID = 0;
INITIALIZE_PASS(YkEliminateSelects, DEBUG_TYPE, "yk-eliminate-selects", false, false)

ModulePass *llvm::createYkEliminateSelectsPass() { return new YkEliminateSelects(); }
