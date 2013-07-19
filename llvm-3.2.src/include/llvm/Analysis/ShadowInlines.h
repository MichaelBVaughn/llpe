// Helper for shadow structures

template<typename T> class ImmutableArray {

  T* arr;
  size_t n;

 public:

 ImmutableArray() : arr(0), n(0) { }
 ImmutableArray(T* _arr, size_t _n) : arr(_arr), n(_n) { }

  ImmutableArray(const ImmutableArray& other) {
    arr = other.arr;
    n = other.n;
  }

  ImmutableArray& operator=(const ImmutableArray& other) {
    arr = other.arr;
    n = other.n;
    return *this;
  }

  T& operator[](size_t n) const {
    return arr[n];
  }

  size_t size() const {
    return n;
  }

  T& back() {
    return arr[size()-1];
  }

};

// Just a tagged union of the types of values that can come out of getOperand.
enum ShadowValType {

  SHADOWVAL_ARG,
  SHADOWVAL_INST,
  SHADOWVAL_GV,
  SHADOWVAL_OTHER,
  SHADOWVAL_INVAL

};

enum va_arg_type {
  
  va_arg_type_none,
  va_arg_type_baseptr,
  va_arg_type_fp,
  va_arg_type_nonfp,
  va_arg_type_any
  
};

class ShadowArg;
class ShadowInstruction;
class ShadowGV;
class InstArgImprovement;
class LocStore;

struct ShadowValue {

  ShadowValType t;
  union {
    ShadowArg* A;
    ShadowInstruction* I;
    ShadowGV* GV;
    Value* V;
  } u;

ShadowValue() : t(SHADOWVAL_INVAL) { u.V = 0; }
ShadowValue(ShadowArg* _A) : t(SHADOWVAL_ARG) { u.A = _A; }
ShadowValue(ShadowInstruction* _I) : t(SHADOWVAL_INST) { u.I = _I; }
ShadowValue(ShadowGV* _GV) : t(SHADOWVAL_GV) { u.GV = _GV; }
ShadowValue(Value* _V) : t(SHADOWVAL_OTHER) { u.V = _V; }

  bool isInval() {
    return t == SHADOWVAL_INVAL;
  }
  bool isArg() {
    return t == SHADOWVAL_ARG;
  }
  bool isInst() {
    return t == SHADOWVAL_INST;
  }
  bool isVal() {
    return t == SHADOWVAL_OTHER;
  }
  ShadowArg* getArg() {
    return t == SHADOWVAL_ARG ? u.A : 0;
  }
  ShadowInstruction* getInst() {
    return t == SHADOWVAL_INST ? u.I : 0;
  }
  Value* getVal() {
    return t == SHADOWVAL_OTHER ? u.V : 0;
  }
  ShadowGV* getGV() {
    return t == SHADOWVAL_GV ? u.GV : 0;
  }

  Type* getType();
  ShadowValue stripPointerCasts();
  IntegrationAttempt* getCtx();
  Value* getBareVal();
  const Loop* getScope();
  const Loop* getNaturalScope();
  bool isIDObject();
  InstArgImprovement* getIAI();
  LLVMContext& getLLVMContext();
  void setCommittedVal(Value* V);
  bool objectAvailableFrom(IntegrationAttempt* OtherIA);
  const MDNode* getTBAATag();
  uint64_t getAllocSize();
  LocStore& getBaseStore();
  int32_t getFrameNo();
  int32_t getHeapKey();
  int32_t getFramePos();

};

inline bool operator==(ShadowValue V1, ShadowValue V2) {
  if(V1.t != V2.t)
    return false;
  switch(V1.t) {
  case SHADOWVAL_INVAL:
    return true;
  case SHADOWVAL_ARG:
    return V1.u.A == V2.u.A;
  case SHADOWVAL_INST:
    return V1.u.I == V2.u.I;
  case SHADOWVAL_GV:
    return V1.u.GV == V2.u.GV;
  case SHADOWVAL_OTHER:
    return V1.u.V == V2.u.V;
  default:
    release_assert(0 && "Bad SV type");
    return false;
  }
}

inline bool operator!=(ShadowValue V1, ShadowValue V2) {
   return !(V1 == V2);
}

inline bool operator<(ShadowValue V1, ShadowValue V2) {
  if(V1.t != V2.t)
    return V1.t < V2.t;
  switch(V1.t) {
  case SHADOWVAL_INVAL:
    return false;
  case SHADOWVAL_ARG:
    return V1.u.A < V2.u.A;
  case SHADOWVAL_INST:
    return V1.u.I < V2.u.I;
  case SHADOWVAL_GV:
    return V1.u.GV < V2.u.GV;
  case SHADOWVAL_OTHER:
    return V1.u.V < V2.u.V;
  default:
    release_assert(0 && "Bad SV type");
    return false;
  }
}

inline bool operator<=(ShadowValue V1, ShadowValue V2) {
  return V1 < V2 || V1 == V2;
}

inline bool operator>(ShadowValue V1, ShadowValue V2) {
  return !(V1 <= V2);
}

inline bool operator>=(ShadowValue V1, ShadowValue V2) {
  return !(V1 < V2);
}

// ImprovedValSetSingle: an SCCP-like value giving candidate constants or pointer base addresses for a value.
// May be: 
// overdefined (overflowed, or defined by an unknown)
// defined (known set of possible values)
// undefined (implied by absence from map)
// Note Value members may be null (signifying a null pointer) without being Overdef.

#define PBMAX 16

enum ValSetType {

  ValSetTypeUnknown,
  ValSetTypePB, // Pointers; the Offset member is set
  ValSetTypeScalar, // Ordinary constants
  ValSetTypeScalarSplat, // Constant splat, used to cheaply express memset(block, size), Offset == size
  ValSetTypeFD, // File descriptors; can only be copied, otherwise opaque
  ValSetTypeVarArg, // Special tokens representing a vararg or VA-related cookie
  ValSetTypeOverdef, // Useful for disambiguating empty PB from Overdef; never actually used in PB.
  ValSetTypeDeallocated, // Special single value signifying an object that is deallocted on a given path.
  ValSetTypeOldOverdef // A special case of Overdef where the value is known not to alias objects
                       // created since specialisation started.

};

bool functionIsBlacklisted(Function*);

struct ImprovedVal {

  ShadowValue V;
  int64_t Offset;

ImprovedVal() : V(), Offset(LLONG_MAX) { }
ImprovedVal(ShadowValue _V, int64_t _O = LLONG_MAX) : V(_V), Offset(_O) { }

  // Values for Offset when this is a VarArg:
  static const int64_t not_va_arg = -1;
  static const int64_t va_baseptr = -2;
  static const int64_t first_nonfp_arg = 0;
  static const int64_t first_fp_arg = 0x00010000;
  static const int64_t first_any_arg = 0x00020000;
  static const int64_t max_arg = 0x00030000;

  int getVaArgType() {

    if(Offset == not_va_arg)
      return va_arg_type_none;
    else if(Offset == va_baseptr)
      return va_arg_type_baseptr;
    else if(Offset >= first_nonfp_arg && Offset < first_fp_arg)
      return va_arg_type_nonfp;
    else if(Offset >= first_fp_arg && Offset < first_any_arg)
      return va_arg_type_fp;
    else if(Offset >= first_any_arg && Offset < max_arg)
      return va_arg_type_any;
    else
      assert(0 && "Bad va_arg value\n");
    return va_arg_type_none;

  }

  int64_t getVaArg() {

    switch(getVaArgType()) {
    case va_arg_type_any:
      return Offset - first_any_arg;
    case va_arg_type_fp:
      return Offset - first_fp_arg;
    case va_arg_type_nonfp:
      return Offset;
    default:
      release_assert(0 && "Bad vaarg type");
      return 0;
    }

  }

  bool isNull() {

    if(Constant* C = dyn_cast_or_null<Constant>(V.getVal()))
      return C->isNullValue();
    else
      return false;

  }

  bool isFunction() {

    return !!dyn_cast_or_null<Function>(V.getVal());

  }

};

inline bool operator==(ImprovedVal V1, ImprovedVal V2) {
  return (V1.V == V2.V && V1.Offset == V2.Offset);
}

inline bool operator!=(ImprovedVal V1, ImprovedVal V2) {
   return !(V1 == V2);
}

inline bool operator<(ImprovedVal V1, ImprovedVal V2) {
  if(V1.V != V2.V)
    return V1.V < V2.V;
  return V1.Offset < V2.Offset;
}

inline bool operator<=(ImprovedVal V1, ImprovedVal V2) {
  return V1 < V2 || V1 == V2;
}

inline bool operator>(ImprovedVal V1, ImprovedVal V2) {
  return !(V1 <= V2);
}

inline bool operator>=(ImprovedVal V1, ImprovedVal V2) {
  return !(V1 < V2);
}

class ImprovedValSetSingle;

typedef std::pair<std::pair<uint64_t, uint64_t>, ImprovedValSetSingle> IVSRange;

struct ImprovedValSet {

  bool isMulti;
ImprovedValSet(bool M) : isMulti(M) { }
  virtual void dropReference() = 0;
  virtual bool isWritableMulti() = 0;
  virtual ImprovedValSet* getReadableCopy() = 0;
  virtual void print(raw_ostream&, bool brief = false) = 0;
  virtual ~ImprovedValSet() {}
  
};

struct ImprovedValSetSingle : public ImprovedValSet {

  static bool classof(const ImprovedValSet* IVS) { return !IVS->isMulti; }

  ValSetType SetType;
  SmallVector<ImprovedVal, 1> Values;
  bool Overdef;

 ImprovedValSetSingle() : ImprovedValSet(false), SetType(ValSetTypeUnknown), Overdef(false) { }
 ImprovedValSetSingle(ValSetType T) : ImprovedValSet(false), SetType(T), Overdef(false) { }
 ImprovedValSetSingle(ValSetType T, bool OD) : ImprovedValSet(false), SetType(T), Overdef(OD) { }
 ImprovedValSetSingle(ImprovedVal V, ValSetType T) : ImprovedValSet(false), SetType(T), Overdef(false) {
    Values.push_back(V);
  }

  virtual ~ImprovedValSetSingle() {}

  virtual void dropReference();

  bool isInitialised() {
    return Overdef || SetType == ValSetTypeDeallocated || SetType == ValSetTypeOldOverdef || Values.size() > 0;
  }

  bool isWhollyUnknown() {
    return Overdef || SetType == ValSetTypeDeallocated || SetType == ValSetTypeOldOverdef || Values.size() == 0;
  }

  bool isOldValue() {
    return (!Overdef) && SetType == ValSetTypeOldOverdef;
  }
  
  void removeValsWithBase(ShadowValue Base) {

    for(SmallVector<ImprovedVal, 1>::iterator it = Values.end(), endit = Values.begin(); it != endit; --it) {

      ImprovedVal& ThisV = *(it - 1);
      if(ThisV.V == Base)
	Values.erase(it);
      
    }

  }

  bool onlyContainsNulls() {

    if(SetType == ValSetTypePB && Values.size() == 1)
      return Values[0].isNull();
    else
      return false;
    
  }

  bool onlyContainsFunctions() {

    if(SetType != ValSetTypeScalar)
      return false;
    
    for(uint32_t i = 0; i < Values.size(); ++i) {

      if(!Values[i].isFunction())
	return false;
      
    }
    
    return true;

  }

  ImprovedValSetSingle& insert(ImprovedVal V) {

    release_assert(V.V.t != SHADOWVAL_INVAL);

    if(Overdef)
      return *this;

    if(SetType == ValSetTypePB) {

      for(SmallVector<ImprovedVal, 1>::iterator it = Values.begin(), endit = Values.end(); it != endit; ++it) {

	if(it->V == V.V) {

	  if(it->Offset != V.Offset)
	    it->Offset = LLONG_MAX;
	  return *this;

	}

      }

    }
    else {

      if(std::count(Values.begin(), Values.end(), V))
	return *this;

    }

    Values.push_back(V);

    if(Values.size() > PBMAX)
      setOverdef();
    
    return *this;

  }

  ImprovedValSetSingle& mergeOne(ValSetType OtherType, ImprovedVal OtherVal) {

    if(OtherType == ValSetTypeUnknown)
      return *this;

    if(OtherType == ValSetTypeOverdef) {
      setOverdef();
      return *this;
    }

    if(isInitialised() && OtherType != SetType) {

      if(onlyContainsFunctions() && OtherVal.isNull()) {

	insert(OtherVal);
	return *this;

      }
      else if(onlyContainsNulls() && OtherVal.isFunction()) {

	SetType = ValSetTypeScalar;
	insert(OtherVal);
	return *this;

      }
      else {

	if(OtherType == ValSetTypePB)
	  SetType = ValSetTypePB;
	setOverdef();
	
      }

    }
    else {

      SetType = OtherType;
      insert(OtherVal);

    }

    return *this;

  }

  ImprovedValSetSingle& merge(ImprovedValSetSingle& OtherPB) {
    if(!OtherPB.isInitialised())
      return *this;
    if(OtherPB.Overdef) {
      if(OtherPB.SetType == ValSetTypePB)
	SetType = ValSetTypePB;
      setOverdef();
    }
    else if(isInitialised() && OtherPB.SetType != SetType) {

      // Deallocated tags are weak, and are overridden by the other value.
      if(SetType == ValSetTypeDeallocated) {
	*this = OtherPB;
	return *this;
      }
      
      if(OtherPB.SetType == ValSetTypeDeallocated) {

	return *this;

      }

      // Special case: functions may permissibly merge with null pointers. In this case
      // reclassify the null as a scalar.
      if(onlyContainsFunctions() && OtherPB.onlyContainsNulls()) {

	insert(OtherPB.Values[0]);
	return *this;

      }
      else if(onlyContainsNulls() && OtherPB.onlyContainsFunctions()) {

	SetType = ValSetTypeScalar;
	// Try again:
	return merge(OtherPB);

      }

      if(OtherPB.SetType == ValSetTypePB)
	SetType = ValSetTypePB;
      setOverdef();

    }
    else {
      SetType = OtherPB.SetType;
      for(SmallVector<ImprovedVal, 4>::iterator it = OtherPB.Values.begin(), it2 = OtherPB.Values.end(); it != it2 && !Overdef; ++it)
	insert(*it);
    }
    return *this;
  }

  void setOverdef() {

    Values.clear();
    Overdef = true;

  }

  void set(ImprovedVal V, ValSetType T) {

    Values.clear();
    Values.push_back(V);
    SetType = T;
    Overdef = false;

  }

  ImprovedValSet* getReadableCopy() {

    return new ImprovedValSetSingle(*this);

  }

  bool isWritableMulti() {
    return false;
  }

  bool coerceToType(llvm::Type* Target, uint64_t TargetSize, std::string* error);
  virtual void print(raw_ostream&, bool brief = false);
  
};

// Traits for half-open integers that never coalesce:

struct HalfOpenNoMerge {
  /// startLess - Return true if x is not in [a;b].
  /// This is x < a both for closed intervals and for [a;b) half-open intervals.
  static inline bool startLess(const uint64_t &x, const uint64_t &a) {
    return x < a;
  }

  /// stopLess - Return true if x is not in [a;b].
  /// This is b < x for a closed interval, b <= x for [a;b) half-open intervals.
  static inline bool stopLess(const uint64_t &b, const uint64_t &x) {
    return b <= x;
  }

  /// adjacent - Return true when the intervals [x;a] and [b;y] can coalesce.
  /// This is a+1 == b for closed intervals, a == b for half-open intervals.
  static inline bool adjacent(const uint64_t &a, const uint64_t &b) {
    return false;
  }

};

struct ImprovedValSetMulti : public ImprovedValSet {

  typedef IntervalMap<uint64_t, ImprovedValSetSingle, IntervalMapImpl::NodeSizer<uint64_t, ImprovedValSetSingle>::LeafSize, HalfOpenNoMerge> MapTy;
  typedef MapTy::iterator MapIt;
  typedef MapTy::const_iterator ConstMapIt;
  MapTy Map;
  uint32_t MapRefCount;
  ImprovedValSet* Underlying;
  uint64_t CoveredBytes;
  uint64_t AllocSize;

  ImprovedValSetMulti(ShadowValue& V);
  ImprovedValSetMulti(uint64_t ASize);
  ImprovedValSetMulti(const ImprovedValSetMulti& other);

  virtual ~ImprovedValSetMulti() {}

  static bool classof(const ImprovedValSet* IVS) { return IVS->isMulti; }
  virtual void dropReference();
  bool isWritableMulti() {
    return MapRefCount == 1;
  }
  ImprovedValSet* getReadableCopy() {
    MapRefCount++;
    return this;
  }

  virtual void print(raw_ostream&, bool brief = false);

};

inline bool IVIsInitialised(ImprovedValSet* IV) {

  if(IV->isMulti)
    return true;
  return cast<ImprovedValSetSingle>(IV)->isInitialised();

}

#define INVALID_BLOCK_IDX 0xffffffff
#define INVALID_INSTRUCTION_IDX 0xffffffff

struct ShadowInstIdx {

  uint32_t blockIdx;
  uint32_t instIdx;

ShadowInstIdx() : blockIdx(INVALID_BLOCK_IDX), instIdx(INVALID_INSTRUCTION_IDX) { }
ShadowInstIdx(uint32_t b, uint32_t i) : blockIdx(b), instIdx(i) { }

};

class ShadowBBInvar;

struct ShadowInstructionInvar {
  
  uint32_t idx;
  Instruction* I;
  ShadowBBInvar* parent;
  ImmutableArray<ShadowInstIdx> operandIdxs;
  ImmutableArray<ShadowInstIdx> userIdxs;
  ImmutableArray<uint32_t> operandBBs;

};

template<class X> inline bool inst_is(ShadowInstructionInvar* SII) {
   return isa<X>(SII->I);
}

template<class X> inline X* dyn_cast_inst(ShadowInstructionInvar* SII) {
  return dyn_cast<X>(SII->I);
}

template<class X> inline X* cast_inst(ShadowInstructionInvar* SII) {
  return cast<X>(SII->I);
}

#define INSTSTATUS_ALIVE 0
#define INSTSTATUS_DEAD 1
#define INSTSTATUS_UNUSED_WRITER 2

struct InstArgImprovement {

  ImprovedValSet* PB;
  uint32_t dieStatus;

InstArgImprovement() : PB(0), dieStatus(INSTSTATUS_ALIVE) { }

};

class ShadowBB;

struct LocStore {

  ImprovedValSet* store;

LocStore(ImprovedValSet* s) : store(s) {}
LocStore() : store(0) {}
LocStore(const LocStore& other) : store(other.store) {}
													    
  LocStore* getReadableCopy() {

    LocStore* copy = new LocStore(*this);
    copy->store = copy->store->getReadableCopy();
    return copy;

  }

};

struct ShadowInstruction {

  ShadowBB* parent;
  ShadowInstructionInvar* invar;
  InstArgImprovement i;
  //SmallVector<ShadowInstruction*, 1> indirectUsers;
  SmallVector<ShadowValue, 1> indirectDIEUsers;
  Value* committedVal;
  // Storage for allocation instructions:
  LocStore store;
  uint64_t storeSize;
  int32_t allocIdx;

  uint32_t getNumOperands() {
    return invar->operandIdxs.size();
  }

  virtual ShadowValue getOperand(uint32_t i);

  ShadowValue getOperandFromEnd(uint32_t i) {
    return getOperand(invar->operandIdxs.size() - i);
  }

  ShadowValue getCallArgOperand(uint32_t i) {
    return getOperand(i);
  }

  uint32_t getNumArgOperands() {
    return getNumOperands() - 1;
  }

  uint32_t getNumUsers() {
    return invar->userIdxs.size();
  }

  ShadowInstruction* getUser(uint32_t i);

  Type* getType() {
    return invar->I->getType();
  }

  bool resolved();

};

template<class X> inline bool inst_is(ShadowInstruction* SI) {
  return inst_is<X>(SI->invar);
}

template<class X> inline X* dyn_cast_inst(ShadowInstruction* SI) {
  return dyn_cast_inst<X>(SI->invar);
}

template<class X> inline X* cast_inst(ShadowInstruction* SI) {
  return cast_inst<X>(SI->invar);
}

struct ShadowArgInvar {

  Argument* A;
  ImmutableArray<ShadowInstIdx> userIdxs;

};

struct ShadowGV {

  GlobalVariable* G;
  LocStore store;
  uint64_t storeSize;
  int32_t allocIdx;

};

struct ShadowArg {

  ShadowArgInvar* invar;
  InlineAttempt* IA;
  InstArgImprovement i;  
  Value* committedVal;

  Type* getType() {
    return invar->A->getType();
  }

};

enum ShadowBBStatus {

  BBSTATUS_UNKNOWN,
  BBSTATUS_CERTAIN,
  BBSTATUS_ASSUMED,
  BBSTATUS_IGNORED

};

struct ShadowFunctionInvar;

struct ShadowBBInvar {

  ShadowFunctionInvar* F;
  uint32_t idx;
  BasicBlock* BB;
  ImmutableArray<uint32_t> succIdxs;
  ImmutableArray<uint32_t> predIdxs;
  ImmutableArray<ShadowInstructionInvar> insts;
  const Loop* outerScope;
  const Loop* naturalScope;

  inline ShadowBBInvar* getPred(uint32_t i);
  inline uint32_t preds_size();

  inline ShadowBBInvar* getSucc(uint32_t i);  
  inline uint32_t succs_size();

};

#define HEAPTREEORDER 16
#define HEAPTREEORDERLOG2 4

class MergeBlockVisitor;

struct SharedTreeNode {

  // These point to SharedTreeNodes or LocStores if this is the bottom layer.
  void* children[HEAPTREEORDER];
  int refCount;

SharedTreeNode() : refCount(1) {

  memset(children, 0, sizeof(void*) * HEAPTREEORDER);

}

  void dropReference(uint32_t height);
  LocStore* getReadableStoreFor(uint32_t idx, uint32_t height);
  LocStore* getOrCreateStoreFor(uint32_t idx, uint32_t height, bool* isNewStore);
  SharedTreeNode* getWritableNode(uint32_t height);
  void mergeHeaps(SmallVector<SharedTreeNode*, 4>& others, bool allOthersClobbered, uint32_t height, uint32_t idx, MergeBlockVisitor* visitor);
  void commitToBase(uint32_t height, uint32_t idx);
  void print(raw_ostream&, bool brief, uint32_t height, uint32_t idx);

};

struct SharedTreeRoot {

  SharedTreeNode* root;
  uint32_t height;

SharedTreeRoot() : root(0), height(0) { }
  void clear();
  void dropReference();
  LocStore* getReadableStoreFor(ShadowValue& V);
  LocStore* getOrCreateStoreFor(ShadowValue& V, bool* isNewStore);
  void growToHeight(uint32_t newHeight);
  void grow(uint32_t idx);
  bool mustGrowFor(uint32_t idx);

};

struct SharedStoreMap {

  std::vector<LocStore> store;
  uint32_t refCount;
  InlineAttempt* IA;
  bool empty;

SharedStoreMap(InlineAttempt* _IA, uint32_t initSize) : store(initSize), refCount(1), IA(_IA), empty(true) { }

  SharedStoreMap* getWritableStoreMap();
  void dropReference();
  void clear();
  SharedStoreMap* getEmptyMap();
  void print(raw_ostream&, bool brief);

};

struct LocalStoreMap {

  SmallVector<SharedStoreMap*, 4> frames;
  SharedTreeRoot heap;

  DenseSet<ShadowValue> threadLocalObjects;
  DenseSet<ShadowValue> noAliasOldObjects;

  bool allOthersClobbered;
  uint32_t refCount;

LocalStoreMap(uint32_t s) : frames(s), heap(), allOthersClobbered(false), refCount(1) {}

  void clear();
  LocalStoreMap* getEmptyMap();
  void dropReference();
  LocalStoreMap* getWritableFrameList();
  void print(raw_ostream&, bool brief = false);
  bool empty();
  void copyEmptyFrames(SmallVector<SharedStoreMap*, 4>&);
  void copyFramesFrom(const LocalStoreMap&);
  std::vector<LocStore>& getWritableFrame(int32_t frameNo);
  void pushStackFrame(InlineAttempt*);
  void popStackFrame();
  LocStore* getReadableStoreFor(ShadowValue& V);
  
};

struct ShadowBB {

  IntegrationAttempt* IA;
  ShadowBBInvar* invar;
  bool* succsAlive;
  ShadowBBStatus status;
  ImmutableArray<ShadowInstruction> insts;
  LocalStoreMap* localStore;
  BasicBlock* committedHead;
  BasicBlock* committedTail;
  bool useSpecialVarargMerge;
  bool inAnyLoop;

  bool edgeIsDead(ShadowBBInvar* BB2I) {

    bool foundLiveEdge = false;

    for(uint32_t i = 0; i < invar->succIdxs.size() && !foundLiveEdge; ++i) {

      if(BB2I->idx == invar->succIdxs[i]) {

	if(succsAlive[i])
	  foundLiveEdge = true;
	
      }

    }

    return !foundLiveEdge;

  }

  DenseMap<ShadowValue, LocStore>& getWritableStoreMapFor(ShadowValue&);
  DenseMap<ShadowValue, LocStore>& getReadableStoreMapFor(ShadowValue&);
  LocStore& getWritableStoreFor(ShadowValue&, int64_t Off, uint64_t Size, bool writeSingleObject);
  LocStore* getOrCreateStoreFor(ShadowValue&, bool* isNewStore);
  LocStore* getReadableStoreFor(ShadowValue& V);
  void pushStackFrame(InlineAttempt*);
  void popStackFrame();
  void setAllObjectsMayAliasOld();
  void setAllObjectsThreadGlobal();
  void clobberMayAliasOldObjects();
  void clobberGlobalObjects();
  void clobberAllExcept(DenseSet<ShadowValue>& Save, bool verbose);

};

struct ShadowLoopInvar {

  uint32_t headerIdx;
  uint32_t preheaderIdx;
  uint32_t latchIdx;
  std::pair<uint32_t, uint32_t> optimisticEdge;
  std::vector<uint32_t> exitingBlocks;
  std::vector<uint32_t> exitBlocks;
  std::vector<std::pair<uint32_t, uint32_t> > exitEdges;
  bool alwaysIterate;
  
};

struct ShadowFunctionInvar {

  ImmutableArray<ShadowBBInvar> BBs;
  ImmutableArray<ShadowArgInvar> Args;
  DenseMap<const Loop*, ShadowLoopInvar*> LInfo;
  int32_t frameSize;

};

ShadowBBInvar* ShadowBBInvar::getPred(uint32_t i) {
  return &(F->BBs[predIdxs[i]]);
}

uint32_t ShadowBBInvar::preds_size() { 
  return predIdxs.size();
}

ShadowBBInvar* ShadowBBInvar::getSucc(uint32_t i) {
  return &(F->BBs[succIdxs[i]]);
}
  
uint32_t ShadowBBInvar::succs_size() {
  return succIdxs.size();
}

inline Type* ShadowValue::getType() {

  switch(t) {
  case SHADOWVAL_ARG:
    return u.A->getType();
  case SHADOWVAL_INST:
    return u.I->getType();
  case SHADOWVAL_GV:
    return u.GV->G->getType();
  case SHADOWVAL_OTHER:
    return u.V->getType();
  case SHADOWVAL_INVAL:
    return 0;
  default:
    release_assert(0 && "Bad SV type");
    return 0;
  }

}

inline const MDNode* ShadowValue::getTBAATag() {

  switch(t) {
  case SHADOWVAL_INST:
    return u.I->invar->I->getMetadata(LLVMContext::MD_tbaa);
  default:
    return 0;
  }

}

inline Value* ShadowValue::getBareVal() {

  switch(t) {
  case SHADOWVAL_ARG:
    return u.A->invar->A;
  case SHADOWVAL_INST:
    return u.I->invar->I;
  case SHADOWVAL_GV:
    return u.GV->G;
  case SHADOWVAL_OTHER:
    return u.V;
  default:
    return 0;
  }

}

inline const Loop* ShadowValue::getScope() {

  switch(t) {
  case SHADOWVAL_INST:
    return u.I->invar->parent->outerScope;
  default:
    return 0;
  }
  
}

inline const Loop* ShadowValue::getNaturalScope() {

  switch(t) {
  case SHADOWVAL_INST:
    return u.I->invar->parent->naturalScope;
  default:
    return 0;
  }

}

extern bool isIdentifiedObject(const Value*);

inline bool ShadowValue::isIDObject() {

  return isIdentifiedObject(getBareVal());

}

inline InstArgImprovement* ShadowValue::getIAI() {

  switch(t) {
  case SHADOWVAL_INST:
    return &(u.I->i);
  case SHADOWVAL_ARG:
    return &(u.A->i);      
  default:
    return 0;
  }

}

inline LLVMContext& ShadowValue::getLLVMContext() {
  switch(t) {
  case SHADOWVAL_INST:
    return u.I->invar->I->getContext();
  case SHADOWVAL_ARG:
    return u.A->invar->A->getContext();
  case SHADOWVAL_GV:
    return u.GV->G->getContext();
  default:
    return u.V->getContext();
  }
}

inline void ShadowValue::setCommittedVal(Value* V) {
  switch(t) {
  case SHADOWVAL_INST:
    u.I->committedVal = V;
    break;
  case SHADOWVAL_ARG:
    u.A->committedVal = V;
    break;
  default:
    release_assert(0 && "Can't replace a value");
  }
}

template<class X> inline bool val_is(ShadowValue V) {
  switch(V.t) {
  case SHADOWVAL_OTHER:
    return isa<X>(V.u.V);
  case SHADOWVAL_GV:
    return isa<X>(V.u.GV->G);
  case SHADOWVAL_INST:
    return inst_is<X>(V.u.I);
  case SHADOWVAL_ARG:
    return isa<X>(V.u.A->invar->A);
  default:
    return false;
  }
}

template<class X> inline X* dyn_cast_val(ShadowValue V) {
  switch(V.t) {
  case SHADOWVAL_OTHER:
    return dyn_cast<X>(V.u.V);
  case SHADOWVAL_GV:
    return dyn_cast<X>(V.u.GV->G);
  case SHADOWVAL_ARG:
    return dyn_cast<X>(V.u.A->invar->A);
  case SHADOWVAL_INST:
    return dyn_cast_inst<X>(V.u.I);
  default:
    return 0;
  }
}

template<class X> inline X* cast_val(ShadowValue V) {
  switch(V.t) {
  case SHADOWVAL_OTHER:
    return cast<X>(V.u.V);
  case SHADOWVAL_GV:
    return cast<X>(V.u.GV->G);
  case SHADOWVAL_ARG:
    return cast<X>(V.u.A->invar->A);
  case SHADOWVAL_INST:
    return cast_inst<X>(V.u.I);
  default:
    release_assert(0 && "Cast of bad SV");
    llvm_unreachable();
  }
}

inline Constant* getSingleConstant(ImprovedValSet* IV) {

  ImprovedValSetSingle* IVS = dyn_cast<ImprovedValSetSingle>(IV);
  if(!IVS)
    return 0;
  if(IVS->Overdef || IVS->Values.size() != 1 || IVS->SetType != ValSetTypeScalar)
    return 0;
  return cast_val<Constant>(IVS->Values[0].V);  

}

inline Constant* getConstReplacement(ShadowArg* SA) {

  if(!SA->i.PB)
    return 0;
  return getSingleConstant(SA->i.PB);

}

inline Constant* getConstReplacement(ShadowInstruction* SI) {

  if(!SI->i.PB)
    return 0;
  return getSingleConstant(SI->i.PB);

}

inline ImprovedValSet* getIVSRef(ShadowValue V);

inline Constant* getConstReplacement(ShadowValue SV) {

  switch(SV.t) {

  case SHADOWVAL_ARG:
  case SHADOWVAL_INST: 
    {
      ImprovedValSet* IVS = getIVSRef(SV);
      if(!IVS)
	return 0;
      return getSingleConstant(IVS);
    }

  default:
    return dyn_cast_or_null<Constant>(SV.getVal());

  }

}

inline ShadowValue tryGetConstReplacement(ShadowValue SV) {

  if(Constant* C = getConstReplacement(SV))
    return ShadowValue(C);
  else
    return SV;

}

std::pair<ValSetType, ImprovedVal> getValPB(Value* V);

inline void getIVOrSingleVal(ShadowValue V, ImprovedValSet*& IVS, std::pair<ValSetType, ImprovedVal>& Single) {

  IVS = 0;

  switch(V.t) {

  case SHADOWVAL_INST:
  case SHADOWVAL_ARG:
    IVS = getIVSRef(V);
    break;
  case SHADOWVAL_GV:
    Single = std::make_pair(ValSetTypePB, ImprovedVal(V, 0));
    break;
  case SHADOWVAL_OTHER:
    Single = getValPB(V.u.V);
    break;
  default:
    release_assert(0 && "Uninit V in getIVOrSingleVal");
    llvm_unreachable();
  }

}

inline bool IVsEqualShallow(ImprovedValSet*, ImprovedValSet*);

inline bool IVMatchesVal(ShadowValue V, ImprovedValSet* IV) {

  ImprovedValSet* OtherIV = 0;
  std::pair<ValSetType, ImprovedVal> OtherSingle;
  getIVOrSingleVal(V, OtherIV, OtherSingle);

  if(OtherIV)
    return IVsEqualShallow(IV, OtherIV);

  ImprovedValSetSingle* IVS = dyn_cast<ImprovedValSetSingle>(IV);
  if(!IVS)
    return false;
  
  if(OtherSingle.first == ValSetTypeOverdef)
    return IVS->Overdef;

  if(OtherSingle.first != IVS->SetType)
    return false;

  if(IVS->Values.size() != 1)
    return false;

  return IVS->Values[0] == OtherSingle.second;

}

inline bool getImprovedValSetSingle(ShadowValue V, ImprovedValSetSingle& OutPB) {

  switch(V.t) {

  case SHADOWVAL_INST:
  case SHADOWVAL_ARG:
    {
      ImprovedValSetSingle* IVS = dyn_cast_or_null<ImprovedValSetSingle>(getIVSRef(V));
      if(!IVS) {
	OutPB.setOverdef();
	return true;
      }
      else {
	OutPB = *IVS;
	return OutPB.isInitialised();
      }	
    }

  case SHADOWVAL_GV:
    OutPB.set(ImprovedVal(V, 0), ValSetTypePB);
    return true;

  case SHADOWVAL_OTHER:
    {
      std::pair<ValSetType, ImprovedVal> VPB = getValPB(V.getVal());
      if(VPB.first == ValSetTypeUnknown)
	return false;
      OutPB.set(VPB.second, VPB.first);
      return true;
    }

  case SHADOWVAL_INVAL:
  default:
    release_assert(0 && "getImprovedValSetSingle on uninit value");
    llvm_unreachable();

  }

}

inline ImprovedValSet* tryGetIVSRef(ShadowValue V) {

  switch(V.t) {
  case SHADOWVAL_INST:
    return V.u.I->i.PB;
  case SHADOWVAL_ARG:
    return V.u.A->i.PB;    
  default:
    return 0;
  }

}

inline ImprovedValSet* getIVSRef(ShadowValue V) {

  release_assert((V.t == SHADOWVAL_INST || V.t == SHADOWVAL_ARG) 
		 && "getIVSRef only applicable to instructions and arguments");
  
  return tryGetIVSRef(V);

}

// V must have an IVS.
inline void addValToPB(ShadowValue& V, ImprovedValSetSingle& ResultPB) {

  switch(V.t) {

  case SHADOWVAL_INST:
  case SHADOWVAL_ARG:
    {
      ImprovedValSetSingle* IVS = cast<ImprovedValSetSingle>(getIVSRef(V));
      if(!IVS->isInitialised())
	ResultPB.setOverdef();
      else
	ResultPB.merge(*IVS);
      return;
    }
  case SHADOWVAL_GV:
    ResultPB.mergeOne(ValSetTypePB, ImprovedVal(V, 0));
    return;
  case SHADOWVAL_OTHER:
    {
      std::pair<ValSetType, ImprovedVal> VPB = getValPB(V.getVal());
      if(VPB.first == ValSetTypeUnknown)
	ResultPB.setOverdef();
      else
	ResultPB.mergeOne(VPB.first, VPB.second);
    }
    return;

  case SHADOWVAL_INVAL:
  default:
    release_assert(0 && "addValToPB on uninit value");
    llvm_unreachable();
    
  }

}

inline bool getBaseAndOffset(ShadowValue SV, ShadowValue& Base, int64_t& Offset, bool ignoreNull = false) {

  ImprovedValSetSingle SVPB;
  if(!getImprovedValSetSingle(SV, SVPB))
    return false;

  if(SVPB.SetType != ValSetTypePB || SVPB.Overdef || SVPB.Values.size() == 0)
    return false;

  if(!ignoreNull) {
    if(SVPB.Values.size() != 1)
      return false;

    Base = SVPB.Values[0].V;
    Offset = SVPB.Values[0].Offset;
    return true;
  }
  else {

    bool setAlready = false;

    // Search for a unique non-null value:
    for(uint32_t i = 0, ilim = SVPB.Values.size(); i != ilim; ++i) {

      if(SVPB.Values[i].V.isVal() && isa<ConstantPointerNull>(SVPB.Values[i].V.getVal()))
	continue;

      if(!setAlready) {
	setAlready = true;
	Base = SVPB.Values[i].V;
	Offset = SVPB.Values[i].Offset;
      }
      else {
	Base = ShadowValue();
	return false;
      }

    }

    return setAlready;

  }

}

inline bool getBaseObject(ShadowValue SV, ShadowValue& Base) {

  int64_t ign;
  return getBaseAndOffset(SV, Base, ign);

}

inline bool getBaseAndConstantOffset(ShadowValue SV, ShadowValue& Base, int64_t& Offset, bool ignoreNull = false) {

  Offset = 0;
  bool ret = getBaseAndOffset(SV, Base, Offset, ignoreNull);
  if(Offset == LLONG_MAX)
    return false;
  return ret;

}

inline bool mayBeReplaced(InstArgImprovement& IAI) {
  ImprovedValSetSingle* IVS = dyn_cast_or_null<ImprovedValSetSingle>(IAI.PB);
  if(!IVS)
    return false;
  return IVS->Values.size() == 1 && (IVS->SetType == ValSetTypeScalar ||
				       (IVS->SetType == ValSetTypePB && IVS->Values[0].Offset != LLONG_MAX) ||
				       IVS->SetType == ValSetTypeFD);
}

inline bool mayBeReplaced(ShadowInstruction* SI) {
  return mayBeReplaced(SI->i);
}

inline bool mayBeReplaced(ShadowArg* SA) {
  return mayBeReplaced(SA->i);
}

inline bool mayBeReplaced(ShadowValue SV) {

  switch(SV.t) {
  case SHADOWVAL_INST:
    return mayBeReplaced(SV.u.I);
  case SHADOWVAL_ARG:
    return mayBeReplaced(SV.u.A);
  default:
    return false;
  }

}

inline void deleteIV(ImprovedValSet* I);
inline ImprovedValSetSingle* newIVS();

inline void setReplacement(InstArgImprovement& IAI, Constant* C) {

  if(IAI.PB)
    deleteIV(IAI.PB);

  ImprovedValSetSingle* NewIVS = newIVS();
  IAI.PB = NewIVS;

  std::pair<ValSetType, ImprovedVal> P = getValPB(C);
  NewIVS->Values.push_back(P.second);
  NewIVS->SetType = P.first;

}

inline void setReplacement(ShadowInstruction* SI, Constant* C) {

  setReplacement(SI->i, C);

}

inline void setReplacement(ShadowArg* SA, Constant* C) {

  setReplacement(SA->i, C);

}

inline ShadowValue ShadowValue::stripPointerCasts() {

  switch(t) {
  case SHADOWVAL_ARG:
  case SHADOWVAL_GV:
    return *this;
  case SHADOWVAL_INST:

    if(inst_is<CastInst>(u.I)) {
      ShadowValue Op = u.I->getOperand(0);
      return Op.stripPointerCasts();
    }
    else {
      return *this;
    }

  case SHADOWVAL_OTHER:
    return u.V->stripPointerCasts();
  default:
    release_assert(0 && "Bad val type in stripPointerCasts");
    llvm_unreachable();
  }

}

// Support functions for AA, which can use bare instructions in a ShadowValue.
// The caller always knows that it's either a bare or a shadowed instruction.

inline ShadowValue getValArgOperand(ShadowValue V, uint32_t i) {

  if(ShadowInstruction* SI = V.getInst())
    return SI->getCallArgOperand(i);
  else {
    CallInst* I = cast<CallInst>(V.getVal());
    return ShadowValue(I->getArgOperand(i));
  }

}


inline ShadowValue getValOperand(ShadowValue V, uint32_t i) {

  if(ShadowInstruction* SI = V.getInst())
    return SI->getOperand(i);
  else {
    Instruction* I = cast<Instruction>(V.getVal());
    return ShadowValue(I->getOperand(i));
  }

}

