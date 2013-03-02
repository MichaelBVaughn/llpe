
#include "llvm/Analysis/HypotheticalConstantFolder.h"

#include "llvm/Instructions.h"
#include "llvm/BasicBlock.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;

static uint32_t DIEProgressN = 0;
const uint32_t DIEProgressLimit = 1000;

static void DIEProgress() {

  if(!mainDIE)
    return;

  DIEProgressN++;
  if(DIEProgressN == DIEProgressLimit) {

    errs() << ".";
    DIEProgressN = 0;

  }

}

bool IntegrationAttempt::shouldDIE(ShadowInstruction* I) {

  if(CallInst* CI = dyn_cast_inst<CallInst>(I)) {
    if(getInlineAttempt(CI))
      return true;
    if(forwardableOpenCalls.count(CI))
      return true;
    return false;
  }

  switch(I->invar->I->getOpcode()) {
  default:
    return true;
  case Instruction::VAArg:
  case Instruction::Alloca:
  case Instruction::Invoke:
  case Instruction::Store:
  case Instruction::Br:
  case Instruction::IndirectBr:
  case Instruction::Switch:
  case Instruction::Unwind:
  case Instruction::Unreachable:
    return false;
  }

}

// Implement a visitor that gets called for every dynamic use of an instruction.

bool IntegrationAttempt::visitNextIterationPHI(ShadowInstruction* I, VisitorContext& Visitor) {

  return false;

}

bool PeelIteration::visitNextIterationPHI(ShadowInstructionInvar* I, VisitorContext& Visitor) {

  if(PHINode* PN = dyn_cast_inst<PHINode>(I)) {

    if(PN->getParent() == L->getHeader()) {

      if(PeelIteration* PI = getNextIteration()) {

	Visitor.visit(PI->getInst(I));

      }
      else {

	Visitor.notifyUsersMissed();

      }

      return true;

    }

  }

  return false;

}

void PeelIteration::visitVariant(ShadowInstructionInvar* VI, VisitorContext& Visitor) {

  const Loop* immediateChild = immediateChildLoop(L, VI->scope);

  PeelAttempt* LPA = getPeelAttempt(immediateChild);
  if(LPA && LPA->isEnabled())
    LPA->visitVariant(VI, Visitor);
  else 
    Visitor.notifyUsersMissed();

}

void PeelAttempt::visitVariant(ShadowInstructionInvar* VI, VisitorContext& Visitor) {

  if(Iterations.back()->iterStatus != IterationStatusFinal)
    Visitor.notifyUsersMissed();

  // Is this a header PHI? If so, this definition-from-outside can only matter for the preheader edge.
  if(VI->scope == L && VI->I->getParent() == L->getHeader() && isa<PHINode>(VI->I)) {

    Visitor.visit(Iterations[0]->getInst(VI));
    return;

  }

  for(std::vector<PeelIteration*>::iterator it = Iterations.begin(), itend = Iterations.end(); it != itend; ++it) {

    if(VILoop == L)
      Visitor.visit((*it)->getInst(VI));
    else
      (*it)->visitVariant(VI, Visitor);

  }

}
  
void IntegrationAttempt::visitExitPHI(ShadowInstructionInvar* UserI, VisitorContext& Visitor) {

  if(parentPA->Iterations.back()->iterStatus == IterationStatusFinal) {
    assert(isa<PHINode>(UserI));
    if(UserI->naturalScope != L)
      parent->visitExitPHI(UserI, Visitor);
    else
      Visitor.visit(getInst(UserI));
  }
  else {
    Visitor.notifyUsersMissed();
  }

}

void IntegrationAttempt::visitUser(ShadowInstIdx& User, VisitorContext& Visitor) {

  // Figure out what context cares about this value. The only possibilities are: this loop iteration, the next iteration of this loop (latch edge of header phi),
  // a child loop (defer to it to decide what to do), or a parent loop (again defer).
  // Note that nested cases (e.g. this is an invariant two children deep) are taken care of in the immediate child or parent's logic.

  if(User.blockIdx == INVALID_BLOCK_IDX || User.instIdx == INVALID_INST_IDX)
    return;

  ShadowInstructionInvar* SII = getInstInvar(User.blockIdx, User.instIdx);
  const Loop* UserL = SII->scope; // The innermost loop on which the user has dependencies (distinct from the loop it actually occupies).

  if(UserL == L) {
	  
    if(!visitNextIterationPHI(SII, Visitor)) {

      // Just an ordinary user in the same iteration (or out of any loop!).
      Visitor.visit(this, UserI);

    }

  }
  else {

    if((!L) || L->contains(UserL)) {

      const Loop* outermostChildLoop = immediateChildLoop(L, UserL);
      // Used in a child loop. Check if that child exists at all and defer to it.

      PeelAttempt* LPA = getPeelAttempt(outermostChildLoop);

      if(LPA && LPA->isEnabled())
	LPA->visitVariant(SII, Visitor);
      else 
	Visitor.notifyUsersMissed();

    }
    else {

      visitExitPHI(SII, Visitor);

    }

  }

}

void IntegrationAttempt::visitUsers(ShadowValue V, VisitorContext& Visitor) {

  ImmutableArray<ShadowInstIdx>* Users;
  if(V.isInst()) {
    Users = &(V.getInst()->invar->userIdxs);
  }
  else {
    Users = &(V.getArg()->invar->userIdxs);
  }
  
  for(uint32_t i = 0; i < Users->size() && Visitor.shouldContinue(); ++i) {

    visitUser((*Users)[i], Visitor);

  }

}

bool llvm::willBeDeleted(ShadowValue V) {

  if(ShadowInst* SI = V.getInst()) {
    return SI->dieStatus != INSTSTATUS_UNKNOWN;
  }
  else if(ShadowArg* SA = V.getArg()) {
    return SA->dieStatus != INSTSTATUS_UNKNOWN;
  }
  else {
    return false;
  }

}

bool llvm::willBeReplacedOrDeleted(ShadowValue V) {

  if(willBeDeleted(V))
    return true;
  if(mayBeReplaced(V)) {

    ShadowValue Base;
    if(getBaseObject(V, Base)) {

      if(Base.getCtx() && !Base.getCtx()->isAvailableFromCtx(V.getCtx()))
	return false;

    }

    return true;

  }
  
  return false;

}

bool llvm::willBeReplacedWithConstantOrDeleted(ShadowValue V) {

  if(willBeDeleted(V))
    return true;
  if(getConstReplacement(V))
    return true;

  return false;

}

class DIVisitor : public VisitorContext {

  ShadowValue V;

public:

  bool maybeLive;

  DIVisitor(ShadowValue _V) : V(_V), maybeLive(false) { }

  virtual void visit(ShadowInstruction* UserI) {

    if(CallInst* CI = dyn_cast_inst<CallInst>(UserI)) {

      if(UserI->parent->IA->isResolvedVFSCall(CI)) {

	// FD arguments to resolved calls are not needed.
	if(V == UserI->getCallArgOperand(0))
	  return;
	
	// The buffer argument isn't needed if the read call will be deleted.
	if(UserI->parent->IA->isUnusedReadCall(CI)) {

	  if(V == UserI->getCallArgOperand(1))
	    return;

	}

      }

      InlineAttempt* IA = UserI->parent->IA->getInlineAttempt(CI);
      if((!IA) || !IA->isEnabled()) {
	DEBUG(dbgs() << "Must assume instruction alive due to use in unexpanded call " << UserI->parent->IA->itcache(*CI) << "\n");
	maybeLive = true;
	return;
      }

      // == called value?
      if(V == UserI->getOperandFromEnd(1)) {
	maybeLive = true;
	return;
      }
      else {

	Function* CalledF = getCalledFunction(UserI);

	if(CalledF) {

	  Function::arg_iterator it = CalledF->arg_begin();
	  for(unsigned i = 0; i < CI->getNumArgOperands(); ++i) {

	    if(UserI->getCallArgOperand(i) == V) {

	      if(it == CalledF->arg_end()) {

		// Varargs
		maybeLive = true;
		return;

	      }
	      else if(!IA->argWillNotUse(i, V)) {

		maybeLive = true;
		return;

	      }

	    }

	    if(it != CalledF->arg_end())
	      ++it;

	  }
	}
	else {
	  maybeLive = true;
	  return;
	}

      }

    }
    else if(willBeReplaced(UserI))
      return;
    else {

      maybeLive = true;

    }

  }
  
  virtual void notifyUsersMissed() {
    maybeLive = true;
  }

  virtual bool shouldContinue() {
    return !maybeLive;
  }

};

bool InlineAttempt::isOwnCallUnused() {

  if(!parent)
    return false;
  else
    return parent->valueIsDead(CI);

}

bool IntegrationAttempt::valueIsDead(ShadowValue V) {

  if(isa<ReturnInst>(V)) {
    
    if(F.getType()->isVoidTy())
      return false;
    InlineAttempt* CallerIA = getFunctionRoot();
    return CallerIA->isOwnCallUnused();

  }
  else {

    DIVisitor DIV(V, this);
    visitUsers(V, DIV);

    return !DIV.maybeLive;

  }

}

// Try to kill all instructions in this context, and if appropriate, arguments.
// Everything should be killed in reverse topological order.
void InlineAttempt::runDIE() {

  // First try to kill our instructions:
  IntegrationAttempt::runDIE();

  // And then our formal arguments:
  for(uint32 i = 0; i < F.arg_size(); ++i) {
    ShadowArg* SA = shadowArgs[i];
    if(willBeReplacedWithConstantOrDeleted(ShadowValue(SI)))
      continue;
    if(valueIsDead(ShadowValue(SA))) {
      SA->dieStatus |= INSTSTATUS_DEAD;
    }
  }

}

void IntegrationAttempt::runDIE() {

  // BBs are already in topological order:
  for(uint32_t i = BBs.size(); i > 0; --i) {

    ShadowBB* BB = BBs[i-1];
    if(!BB)
      continue;

    if(BB->invar->naturalScope != L) {

      const Loop* EnterL = immediateChildLoop(L, BB->invar->naturalScope);
      if(PeelAttempt* LPA = getPeelAttempt(EnterL)) {

	for(int i = LPA->Iterations.size() - 1; i >= 0; --i) {

	  LPA->Iterations[i]->runDIE();
	  
	}

      }

      // Skip loop blocks regardless of whether we entered the loop:
      while(i > 0 && BBs[i-1] && BBs[i-1]->invar->naturalScope && enterLoop->contains(BBs[i-1]->invar->naturalScope))
	--i;
      continue;

    }

    for(uint32_t j = BB->insts.size(); j > 0; --j) {

      DIEProgress();

      ShadowInstruction* SI = BB->insts[j-1];

      if(!shouldDIE(SI))
	continue;
      if(willBeReplacedWithConstantOrDeleted(ShadowValue(SI)))
	continue;

      CallInst* CI = dyn_cast_inst<CallInst>(SI);

      if(CI) {
	if(InlineAttempt* IA = getInlineAttempt(CI)) {

	  IA->runDIE();

	}
      }

      if(SI->invar->I->mayHaveSideEffects() && ((!CI) || !forwardableOpenCalls.count(CI))) {

	continue;

      }

      if(valueIsDead(ShadowValue(SI))) {

	SI->dieStatus |= INSTSTATUS_DEAD;

      }

    }

  }

}