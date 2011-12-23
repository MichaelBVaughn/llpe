//===- PHITransAddr.h - PHI Translation for Addresses -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the PHITransAddr class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PHITRANSADDR_H
#define LLVM_ANALYSIS_PHITRANSADDR_H

#include "llvm/Instruction.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/HypotheticalConstantFolder.h"

namespace llvm {
  class DominatorTree;
  class TargetData;
  
/// PHITransAddr - An address value which tracks and handles phi translation.
/// As we walk "up" the CFG through predecessors, we need to ensure that the
/// address we're tracking is kept up to date.  For example, if we're analyzing
/// an address of "&A[i]" and walk through the definition of 'i' which is a PHI
/// node, we *must* phi translate i to get "&A[j]" or else we will analyze an
/// incorrect pointer in the predecessor block.
///
/// This is designed to be a relatively small object that lives on the stack and
/// is copyable.
///
class PHITransAddr {
  /// Addr - The actual address we're analyzing.
  Value *Addr;
  
  /// TD - The target data we are playing with if known, otherwise null.
  const TargetData *TD;

  HCFParentCallbacks* parent;
  
  /// InstInputs - The inputs for our symbolic address.
  SmallVector<Instruction*, 4> InstInputs;
public:
  PHITransAddr(Value *addr, const TargetData *td, HCFParentCallbacks* P = 0) : Addr(addr), TD(td), parent(p) {
    // If the address is an instruction, the whole thing is considered an input.
    if (Instruction *I = dyn_cast<Instruction>(Addr))
      InstInputs.push_back(I);
  }
  
  Value *getAddr() const { return Addr; }
  
  /// NeedsPHITranslationFromBlock - Return true if moving from the specified
  /// BasicBlock to its predecessors requires PHI translation.
  bool NeedsPHITranslationFromBlock(BasicBlock *BB) const {
    // We do need translation if one of our input instructions is defined in
    // this block.
    for (unsigned i = 0, e = InstInputs.size(); i != e; ++i)
      if (InstInputs[i]->getParent() == BB)
        return true;
    return false;
  }
  
  /// IsPotentiallyPHITranslatable - If this needs PHI translation, return true
  /// if we have some hope of doing it.  This should be used as a filter to
  /// avoid calling PHITranslateValue in hopeless situations.
  bool IsPotentiallyPHITranslatable() const;
  
  /// PHITranslateValue - PHI translate the current address up the CFG from
  /// CurBB to Pred, updating our state to reflect any needed changes.  If the
  /// dominator tree DT is non-null, the translated value must dominate
  /// PredBB.  This returns true on failure and sets Addr to null.
  bool PHITranslateValue(BasicBlock *CurBB, BasicBlock *PredBB,
                         const DominatorTree *DT);
  
  /// PHITranslateWithInsertion - PHI translate this value into the specified
  /// predecessor block, inserting a computation of the value if it is
  /// unavailable.
  ///
  /// All newly created instructions are added to the NewInsts list.  This
  /// returns null on failure.
  ///
  Value *PHITranslateWithInsertion(BasicBlock *CurBB, BasicBlock *PredBB,
                                   const DominatorTree &DT,
                                   SmallVectorImpl<Instruction*> &NewInsts);
  
  void dump() const;
  
  /// Verify - Check internal consistency of this data structure.  If the
  /// structure is valid, it returns true.  If invalid, it prints errors and
  /// returns false.
  bool Verify() const;
  bool VerifySubExpr(const Value *Expr, SmallVectorImpl<Instruction*> &InstInputs) const;

private:
  Value *PHITranslateSubExpr(Value *V, BasicBlock *CurBB, BasicBlock *PredBB,
                             const DominatorTree *DT);
  
  /// InsertPHITranslatedSubExpr - Insert a computation of the PHI translated
  /// version of 'V' for the edge PredBB->CurBB into the end of the PredBB
  /// block.  All newly created instructions are added to the NewInsts list.
  /// This returns null on failure.
  ///
  Value *InsertPHITranslatedSubExpr(Value *InVal, BasicBlock *CurBB,
                                    BasicBlock *PredBB, const DominatorTree &DT,
                                    SmallVectorImpl<Instruction*> &NewInsts);

  bool CanPHITrans(const Instruction *Inst) const;
  
  /// AddAsInput - If the specified value is an instruction, add it as an input.
  Value *AddAsInput(Value *V) {
    // If V is an instruction, it is now an input.
    if (Instruction *VI = dyn_cast<Instruction>(V))
      InstInputs.push_back(VI);
    return V;
  }
  
};

} // end namespace llvm

#endif
