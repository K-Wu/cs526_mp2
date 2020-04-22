// /// A helper class for numbering instructions in multiple blocks.
// /// Numbers start at zero for each basic block.
// struct BlockNumbering {

//   BlockNumbering(BasicBlock *Bb) : BB(Bb), Valid(false) {}

//   void numberInstructions() {
//     unsigned Loc = 0;
//     InstrIdx.clear();
//     InstrVec.clear();
//     // Number the instructions in the block.
//     for (BasicBlock::iterator it = BB->begin(), e = BB->end(); it != e; ++it) {
//       InstrIdx[it] = Loc++;
//       InstrVec.push_back(it);
//       assert(InstrVec[InstrIdx[it]] == it && "Invalid allocation");
//     }
//     Valid = true;
//   }

//   int getIndex(Instruction *I) {
//     assert(I->getParent() == BB && "Invalid instruction");
//     if (!Valid)
//       numberInstructions();
//     assert(InstrIdx.count(I) && "Unknown instruction");
//     return InstrIdx[I];
//   }

//   Instruction *getInstruction(unsigned loc) {
//     if (!Valid)
//       numberInstructions();
//     assert(InstrVec.size() > loc && "Invalid Index");
//     return InstrVec[loc];
//   }

//   void forget() { Valid = false; }

// private:
//   /// The block we are numbering.
//   BasicBlock *BB;
//   /// Is the block numbered.
//   bool Valid;
//   /// Maps instructions to numbers and back.
//   SmallDenseMap<Instruction *, int> InstrIdx;
//   /// Maps integers to Instructions.
//   SmallVector<Instruction *, 32> InstrVec;
// };

#pragma once

/// Bottom Up SLP Vectorizer.
class BoUpSLP
{
public:
  typedef SmallVector<Value *, 8> ValueList;
  typedef SmallVector<Instruction *, 16> InstrList;
  typedef SmallPtrSet<Value *, 16> ValueSet;
  typedef SmallVector<StoreInst *, 8> StoreList;

  BoUpSLP(Function *Func, ScalarEvolution *Se, const DataLayout *Dl,
          TargetTransformInfo *Tti, TargetLibraryInfo *TLi, AliasAnalysis *Aa,
          LoopInfo *Li, DominatorTree *Dt)
      : F(Func), SE(Se), DL(Dl), TTI(Tti), TLI(TLi), AA(Aa), LI(Li), DT(Dt),
        Builder(Se->getContext())
  {
  }

  /// \brief Vectorize the tree that starts with the elements in \p VL.
  /// Returns the vectorized root.
  Value *vectorizeTree();

  /// \returns the vectorization cost of the subtree that starts at \p VL.
  /// A negative number means that this is profitable.
  int getTreeCost();

  /// Construct a vectorizable tree that starts at \p Roots, ignoring users for
  /// the purpose of scheduling and extraction in the \p UserIgnoreLst.
  void buildTree(ArrayRef<Value *> Roots); //,
                                           //ArrayRef<Value *> UserIgnoreLst = None);

  /// Clear the internal data structures that are created by 'buildTree'.
  void deleteTree()
  {
    VectorizableTree.clear();
    ScalarToTreeEntry.clear();
    MustGather.clear();
    ExternalUses.clear();
    MemBarrierIgnoreList.clear();

    for (auto &Iter : BlocksSchedules)
    {
      BlockScheduling *BS = Iter.second.get();
      BS->clear();
    }
  }

  /// \returns true if the memory operations A and B are consecutive.
  bool isConsecutiveAccess(Value *A, Value *B);

  /// \brief Perform LICM and CSE on the newly generated gather sequences.
  //void optimizeGatherSequence();

  /// Contains all scheduling relevant data for an instruction.
  /// A ScheduleData either represents a single instruction or a member of an
  /// instruction bundle (= a group of instructions which is combined into a
  /// vector instruction).
  struct ScheduleData
  {

    // The initial value for the dependency counters. It means that the
    // dependencies are not calculated yet.
    enum
    {
      InvalidDeps = -1
    };

    ScheduleData()
        : Inst(nullptr), FirstInBundle(nullptr), NextInBundle(nullptr),
          NextLoadStore(nullptr), SchedulingRegionID(0), SchedulingPriority(0),
          Dependencies(InvalidDeps), UnscheduledDeps(InvalidDeps),
          UnscheduledDepsInBundle(InvalidDeps), IsScheduled(false) {}

    void init(int BlockSchedulingRegionID)
    {
      FirstInBundle = this;
      NextInBundle = nullptr;
      NextLoadStore = nullptr;
      IsScheduled = false;
      SchedulingRegionID = BlockSchedulingRegionID;
      UnscheduledDepsInBundle = UnscheduledDeps;
      clearDependencies();
    }

    /// Returns true if the dependency information has been calculated.
    bool hasValidDependencies() const { return Dependencies != InvalidDeps; }

    /// Returns true for single instructions and for bundle representatives
    /// (= the head of a bundle).
    bool isSchedulingEntity() const { return FirstInBundle == this; }

    /// Returns true if it represents an instruction bundle and not only a
    /// single instruction.
    bool isPartOfBundle() const
    {
      return NextInBundle != nullptr || FirstInBundle != this;
    }

    /// Returns true if it is ready for scheduling, i.e. it has no more
    /// unscheduled depending instructions/bundles.
    bool isReady() const
    {
      assert(isSchedulingEntity() &&
             "can't consider non-scheduling entity for ready list");
      return UnscheduledDepsInBundle == 0 && !IsScheduled;
    }

    /// Modifies the number of unscheduled dependencies, also updating it for
    /// the whole bundle.
    int incrementUnscheduledDeps(int Incr)
    {
      UnscheduledDeps += Incr;
      return FirstInBundle->UnscheduledDepsInBundle += Incr;
    }

    /// Sets the number of unscheduled dependencies to the number of
    /// dependencies.
    void resetUnscheduledDeps()
    {
      incrementUnscheduledDeps(Dependencies - UnscheduledDeps);
    }

    /// Clears all dependency information.
    void clearDependencies()
    {
      Dependencies = InvalidDeps;
      resetUnscheduledDeps();
      MemoryDependencies.clear();
    }

    void dump(raw_ostream &os) const
    {
      if (!isSchedulingEntity())
      {
        os << "/ " << *Inst;
      }
      else if (NextInBundle)
      {
        os << '[' << *Inst;
        ScheduleData *SD = NextInBundle;
        while (SD)
        {
          os << ';' << *SD->Inst;
          SD = SD->NextInBundle;
        }
        os << ']';
      }
      else
      {
        os << *Inst;
      }
    }

    Instruction *Inst;

    /// Points to the head in an instruction bundle (and always to this for
    /// single instructions).
    ScheduleData *FirstInBundle;

    /// Single linked list of all instructions in a bundle. Null if it is a
    /// single instruction.
    ScheduleData *NextInBundle;

    /// Single linked list of all memory instructions (e.g. load, store, call)
    /// in the block - until the end of the scheduling region.
    ScheduleData *NextLoadStore;

    /// The dependent memory instructions.
    /// This list is derived on demand in calculateDependencies().
    SmallVector<ScheduleData *, 4> MemoryDependencies;

    /// This ScheduleData is in the current scheduling region if this matches
    /// the current SchedulingRegionID of BlockScheduling.
    int SchedulingRegionID;

    /// Used for getting a "good" final ordering of instructions.
    int SchedulingPriority;

    /// The number of dependencies. Constitutes of the number of users of the
    /// instruction plus the number of dependent memory instructions (if any).
    /// This value is calculated on demand.
    /// If InvalidDeps, the number of dependencies is not calculated yet.
    ///
    int Dependencies;

    /// The number of dependencies minus the number of dependencies of scheduled
    /// instructions. As soon as this is zero, the instruction/bundle gets ready
    /// for scheduling.
    /// Note that this is negative as long as Dependencies is not calculated.
    int UnscheduledDeps;

    /// The sum of UnscheduledDeps in a bundle. Equals to UnscheduledDeps for
    /// single instructions.
    int UnscheduledDepsInBundle;

    /// True if this instruction is scheduled (or considered as scheduled in the
    /// dry-run).
    bool IsScheduled;
  };

  /// Contains all scheduling data for a basic block.
  ///
  struct BlockScheduling
  {

    BlockScheduling(BasicBlock *BB)
        : BB(BB), ChunkSize(BB->size()), ChunkPos(ChunkSize),
          ScheduleStart(nullptr), ScheduleEnd(nullptr),
          FirstLoadStoreInRegion(nullptr), LastLoadStoreInRegion(nullptr),
          ScheduleRegionSize(0),
          ScheduleRegionSizeLimit(ScheduleRegionSizeBudget),
          // Make sure that the initial SchedulingRegionID is greater than the
          // initial SchedulingRegionID in ScheduleData (which is 0).
          SchedulingRegionID(1)
    {
    }

    void clear()
    {
      ReadyInsts.clear();
      ScheduleStart = nullptr;
      ScheduleEnd = nullptr;
      FirstLoadStoreInRegion = nullptr;
      LastLoadStoreInRegion = nullptr;

      // Reduce the maximum schedule region size by the size of the
      // previous scheduling run.
      ScheduleRegionSizeLimit -= ScheduleRegionSize;
      if (ScheduleRegionSizeLimit < MinScheduleRegionSize)
        ScheduleRegionSizeLimit = MinScheduleRegionSize;
      ScheduleRegionSize = 0;

      // Make a new scheduling region, i.e. all existing ScheduleData is not
      // in the new region yet.
      ++SchedulingRegionID;
    }

    ScheduleData *getScheduleData(Value *V)
    {
      ScheduleData *SD = ScheduleDataMap[V];
      if (SD && SD->SchedulingRegionID == SchedulingRegionID)
        return SD;
      return nullptr;
    }

    bool isInSchedulingRegion(ScheduleData *SD)
    {
      return SD->SchedulingRegionID == SchedulingRegionID;
    }

    /// Marks an instruction as scheduled and puts all dependent ready
    /// instructions into the ready-list.
    template <typename ReadyListType>
    void schedule(ScheduleData *SD, ReadyListType &ReadyList)
    {
      SD->IsScheduled = true;
      LLVM_DEBUG(dbgs() << "SLP:   schedule " << *SD << "\n");

      ScheduleData *BundleMember = SD;
      while (BundleMember)
      {
        // Handle the def-use chain dependencies.
        for (Use &U : BundleMember->Inst->operands())
        {
          ScheduleData *OpDef = getScheduleData(U.get());
          if (OpDef && OpDef->hasValidDependencies() &&
              OpDef->incrementUnscheduledDeps(-1) == 0)
          {
            // There are no more unscheduled dependencies after decrementing,
            // so we can put the dependent instruction into the ready list.
            ScheduleData *DepBundle = OpDef->FirstInBundle;
            assert(!DepBundle->IsScheduled &&
                   "already scheduled bundle gets ready");
            ReadyList.insert(DepBundle);
            LLVM_DEBUG(dbgs() << "SLP:    gets ready (def): " << *DepBundle << "\n");
          }
        }
        // Handle the memory dependencies.
        for (ScheduleData *MemoryDepSD : BundleMember->MemoryDependencies)
        {
          if (MemoryDepSD->incrementUnscheduledDeps(-1) == 0)
          {
            // There are no more unscheduled dependencies after decrementing,
            // so we can put the dependent instruction into the ready list.
            ScheduleData *DepBundle = MemoryDepSD->FirstInBundle;
            assert(!DepBundle->IsScheduled &&
                   "already scheduled bundle gets ready");
            ReadyList.insert(DepBundle);
            LLVM_DEBUG(dbgs() << "SLP:    gets ready (mem): " << *DepBundle << "\n");
          }
        }
        BundleMember = BundleMember->NextInBundle;
      }
    }

    /// Put all instructions into the ReadyList which are ready for scheduling.
    template <typename ReadyListType>
    void initialFillReadyList(ReadyListType &ReadyList)
    {
      for (auto *I = ScheduleStart; I != ScheduleEnd; I = I->getNextNode())
      {
        ScheduleData *SD = getScheduleData(I);
        if (SD->isSchedulingEntity() && SD->isReady())
        {
          ReadyList.insert(SD);
          LLVM_DEBUG(dbgs() << "SLP:    initially in ready list: " << *I << "\n");
        }
      }
    }

    /// Checks if a bundle of instructions can be scheduled, i.e. has no
    /// cyclic dependencies. This is only a dry-run, no instructions are
    /// actually moved at this stage.
    bool tryScheduleBundle(ArrayRef<Value *> VL, BoUpSLP *SLP);

    /// Un-bundles a group of instructions.
    void cancelScheduling(ArrayRef<Value *> VL);

    /// Extends the scheduling region so that V is inside the region.
    /// \returns true if the region size is within the limit.
    bool extendSchedulingRegion(Value *V);

    /// Initialize the ScheduleData structures for new instructions in the
    /// scheduling region.
    void initScheduleData(Instruction *FromI, Instruction *ToI,
                          ScheduleData *PrevLoadStore,
                          ScheduleData *NextLoadStore);

    /// Updates the dependency information of a bundle and of all instructions/
    /// bundles which depend on the original bundle.
    void calculateDependencies(ScheduleData *SD, bool InsertInReadyList,
                               BoUpSLP *SLP);

    /// Sets all instruction in the scheduling region to un-scheduled.
    void resetSchedule();

    BasicBlock *BB;

    /// Simple memory allocation for ScheduleData.
    std::vector<std::unique_ptr<ScheduleData[]>> ScheduleDataChunks;

    /// The size of a ScheduleData array in ScheduleDataChunks.
    int ChunkSize;

    /// The allocator position in the current chunk, which is the last entry
    /// of ScheduleDataChunks.
    int ChunkPos;

    /// Attaches ScheduleData to Instruction.
    /// Note that the mapping survives during all vectorization iterations, i.e.
    /// ScheduleData structures are recycled.
    DenseMap<Value *, ScheduleData *> ScheduleDataMap;

    struct ReadyList : SmallVector<ScheduleData *, 8>
    {
      void insert(ScheduleData *SD) { push_back(SD); }
    };

    /// The ready-list for scheduling (only used for the dry-run).
    ReadyList ReadyInsts;

    /// The first instruction of the scheduling region.
    Instruction *ScheduleStart;

    /// The first instruction _after_ the scheduling region.
    Instruction *ScheduleEnd;

    /// The first memory accessing instruction in the scheduling region
    /// (can be null).
    ScheduleData *FirstLoadStoreInRegion;

    /// The last memory accessing instruction in the scheduling region
    /// (can be null).
    ScheduleData *LastLoadStoreInRegion;

    /// The current size of the scheduling region.
    int ScheduleRegionSize;

    /// The maximum size allowed for the scheduling region.
    int ScheduleRegionSizeLimit;

    /// The ID of the scheduling region. For a new vectorization iteration this
    /// is incremented which "removes" all ScheduleData from the region.
    int SchedulingRegionID;
  };

private:
  struct TreeEntry;

  /// \returns the cost of the vectorizable entry.
  int getEntryCost(TreeEntry *E);

  /// This is the recursive part of buildTree.
  void buildTree_rec(ArrayRef<Value *> Roots, unsigned Depth);

  /// Vectorize a single entry in the tree.
  Value *do_vectorizeTree_rec(TreeEntry *E);

  /// Vectorize a single entry in the tree, starting in \p VL.
  Value *vectorizeTree_rec(ArrayRef<Value *> VL);

  /// \returns the pointer to the vectorized value if \p VL is already
  /// vectorized, or NULL. They may happen in cycles.
  Value *alreadyVectorized(ArrayRef<Value *> VL) const;

  /// \brief Take the pointer operand from the Load/Store instruction.
  /// \returns NULL if this is not a valid Load/Store instruction.
  static Value *getPointerOperand(Value *I);

  /// \brief Take the address space operand from the Load/Store instruction.
  /// \returns -1 if this is not a valid Load/Store instruction.
  static unsigned getAddressSpaceOperand(Value *I);

  /// \returns the scalarization cost for this type. Scalarization in this
  /// context means the creation of vectors from a group of scalars.
  int getGatherCost(Type *Ty);

  /// \returns the scalarization cost for this list of values. Assuming that
  /// this subtree gets vectorized, we may need to extract the values from the
  /// roots. This method calculates the cost of extracting the values.
  int getGatherCost(ArrayRef<Value *> VL);

  /// \brief Checks if it is possible to sink an instruction from
  /// \p Src to \p Dst.
  /// \returns the pointer to the barrier instruction if we can't sink.
  //Value *getSinkBarrier(Instruction *Src, Instruction *Dst); //removed in rl214494

  /// \returns the index of the last instruction in the BB from \p VL.
  //int getLastIndex(ArrayRef<Value *> VL); //removed in rl214494

  /// \returns the Instruction in the bundle \p VL.
  //Instruction *getLastInstruction(ArrayRef<Value *> VL); //removed in rl214494

  /// \brief Set the Builder insert point to one after the last instruction in
  /// the bundle
  void setInsertPointAfterBundle(ArrayRef<Value *> VL);

  /// \returns a vector from a collection of scalars in \p VL.
  Value *Gather(ArrayRef<Value *> VL, VectorType *Ty);

  /// \returns whether the VectorizableTree is fully vectoriable and will
  /// be beneficial even the tree height is tiny.
  bool isFullyVectorizableTinyTree();

  struct TreeEntry
  {
    TreeEntry() : Scalars(), VectorizedValue(nullptr),
                  NeedToGather(0) {}

    /// \returns true if the scalars in VL are equal to this entry.
    bool isSame(ArrayRef<Value *> VL) const
    {
      assert(VL.size() == Scalars.size() && "Invalid size");
      return std::equal(VL.begin(), VL.end(), Scalars.begin());
    }

    /// A vector of scalars.
    ValueList Scalars;

    /// The Scalars are vectorized into this value. It is initialized to Null.
    Value *VectorizedValue;

    /// The index in the basic block of the last scalar.
    //int LastScalarIndex; //removed in rl214494

    /// Do we need to gather this sequence ?
    bool NeedToGather;
  };
  /// Create a new VectorizableTree entry.
  TreeEntry *newTreeEntry(ArrayRef<Value *> VL, bool Vectorized)
  {

    VectorizableTree.push_back(TreeEntry());
    int idx = VectorizableTree.size() - 1;
    TreeEntry *Last = &VectorizableTree[idx];
    Last->Scalars.insert(Last->Scalars.begin(), VL.begin(), VL.end());
    Last->NeedToGather = !Vectorized;
    if (Vectorized)
    {
      dbg_executes(errs() << "vectorized tree entry: ";);
    }
    else
    {
      dbg_executes(errs() << "non-vectorized tree entry: ";);
    }
    for (int idx = 0; idx < VL.size(); idx++)
    {
      dbg_executes(errs() << " , " << *(VL[idx]););
    }
    dbg_executes(errs() << "\n";);

    if (Vectorized)
    {
      for (int i = 0, e = VL.size(); i != e; ++i)
      {
        assert(!ScalarToTreeEntry.count(VL[i]) && "Scalar already in tree!");
        ScalarToTreeEntry[VL[i]] = idx;
      }
    }
    else
    {
      MustGather.insert(VL.begin(), VL.end());
    }
    return Last;
  }

  /// Checks if two instructions may access the same memory.
  ///
  /// \p Loc1 is the location of \p Inst1. It is passed explicitly because it
  /// is invariant in the calling loop.
  bool isAliased(const MemoryLocation &Loc1, Instruction *Inst1,
                 Instruction *Inst2)
  {

    // First check if the result is already in the cache.
    AliasCacheKey key = std::make_pair(Inst1, Inst2);
    Optional<bool> &result = AliasCache[key];
    if (result.hasValue())
    {
      return result.getValue();
    }
    MemoryLocation Loc2 = getLocation(Inst2, AA);
    bool aliased = true;
    if (Loc1.Ptr && Loc2.Ptr && isSimple(Inst1) && isSimple(Inst2))
    {
      // Do the alias check.
      //dbg_executes(errs() << "AA isaliased " << Loc1.Ptr << " , " << Loc2.Ptr << " , " << AA->alias(Loc1, Loc2) << "\n";);
      aliased = AA->alias(Loc1, Loc2);
    }
    // Store the result in the cache.
    result = aliased;
    return aliased;
  }

  using AliasCacheKey = std::pair<Instruction *, Instruction *>;
  /// Cache for alias results.
  /// TODO: consider moving this to the AliasAnalysis itself.
  DenseMap<AliasCacheKey, Optional<bool>> AliasCache;

  /// -- Vectorization State --
  /// Holds all of the tree entries.
  std::vector<TreeEntry> VectorizableTree;

  /// Maps a specific scalar to its tree entry.
  SmallDenseMap<Value *, int> ScalarToTreeEntry;

  /// A list of scalars that we found that we need to keep as scalars.
  ValueSet MustGather;

  /// This POD struct describes one external user in the vectorized tree.
  struct ExternalUser
  {
    ExternalUser(Value *S, llvm::User *U, int L) : Scalar(S), User(U), Lane(L){};
    // Which scalar in our function.
    Value *Scalar;
    // Which user that uses the scalar.
    llvm::User *User;
    // Which lane does the scalar belong to.
    int Lane;
  };
  typedef SmallVector<ExternalUser, 16> UserList;

  /// A list of values that need to extracted out of the tree.
  /// This list holds pairs of (Internal Scalar : External User).
  UserList ExternalUses;

  /// A list of instructions to ignore while sinking
  /// memory instructions. This map must be reset between runs of getCost.
  ValueSet MemBarrierIgnoreList;

  /// Holds all of the instructions that we gathered.
  //SetVector<Instruction *> GatherSeq; //don't need as we don't optimize gather seq
  /// A list of blocks that we are going to CSE.
  //SetVector<BasicBlock *> CSEBlocks; //don't need as we don't optimize gather seq

  friend inline raw_ostream &operator<<(raw_ostream &os,
                                        const BoUpSLP::ScheduleData &SD)
  {
    SD.dump(os);
    return os;
  }

  /// Attaches the BlockScheduling structures to basic blocks.
  DenseMap<BasicBlock *, std::unique_ptr<BlockScheduling>> BlocksSchedules;

  /// Performs the "real" scheduling. Done before vectorization is actually
  /// performed in a basic block.
  void scheduleBlock(BlockScheduling *BS);

  /// List of users to ignore during scheduling and that don't need extracting.
  ArrayRef<Value *> UserIgnoreList;

  // Analysis and block reference.
  Function *F;
  ScalarEvolution *SE;
  const DataLayout *DL;
  TargetTransformInfo *TTI;
  TargetLibraryInfo *TLI;
  AliasAnalysis *AA;
  LoopInfo *LI;
  DominatorTree *DT;
  /// Instruction builder to construct the vectorized tree.
  IRBuilder<> Builder;
};

Value *BoUpSLP::getPointerOperand(Value *I)
{
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->getPointerOperand();
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->getPointerOperand();
  return nullptr;
}

unsigned BoUpSLP::getAddressSpaceOperand(Value *I)
{
  if (LoadInst *L = dyn_cast<LoadInst>(I))
    return L->getPointerAddressSpace();
  if (StoreInst *S = dyn_cast<StoreInst>(I))
    return S->getPointerAddressSpace();
  return -1;
}

bool BoUpSLP::isConsecutiveAccess(Value *A, Value *B)
{
  Value *PtrA = getPointerOperand(A);
  Value *PtrB = getPointerOperand(B);
  unsigned ASA = getAddressSpaceOperand(A);
  unsigned ASB = getAddressSpaceOperand(B);

  // Check that the address spaces match and that the pointers are valid.
  if (!PtrA || !PtrB || (ASA != ASB))
    return false;

  // Make sure that A and B are different pointers of the same type.
  if (PtrA == PtrB || PtrA->getType() != PtrB->getType())
    return false;

  unsigned PtrBitWidth = DL->getPointerSizeInBits(ASA);
  Type *Ty = cast<PointerType>(PtrA->getType())->getElementType();
  APInt Size(PtrBitWidth, DL->getTypeStoreSize(Ty));

  APInt OffsetA(PtrBitWidth, 0), OffsetB(PtrBitWidth, 0);
  PtrA = PtrA->stripAndAccumulateInBoundsConstantOffsets(*DL, OffsetA);
  PtrB = PtrB->stripAndAccumulateInBoundsConstantOffsets(*DL, OffsetB);

  APInt OffsetDelta = OffsetB - OffsetA;

  // Check if they are based on the same pointer. That makes the offsets
  // sufficient.
  if (PtrA == PtrB)
    return OffsetDelta == Size;

  // Compute the necessary base pointer delta to have the necessary final delta
  // equal to the size.
  APInt BaseDelta = Size - OffsetDelta;

  // Otherwise compute the distance with SCEV between the base pointers.
  const SCEV *PtrSCEVA = SE->getSCEV(PtrA);
  const SCEV *PtrSCEVB = SE->getSCEV(PtrB);
  const SCEV *C = SE->getConstant(BaseDelta);
  const SCEV *X = SE->getAddExpr(PtrSCEVA, C);
  return X == PtrSCEVB;
}

// Groups the instructions to a bundle (which is then a single scheduling entity)
// and schedules instructions until the bundle gets ready.
bool BoUpSLP::BlockScheduling::tryScheduleBundle(ArrayRef<Value *> VL,
                                                 BoUpSLP *SLP)
{
  if (isa<PHINode>(VL[0]))
    return true;

  // Initialize the instruction bundle.
  Instruction *OldScheduleEnd = ScheduleEnd;
  ScheduleData *PrevInBundle = nullptr;
  ScheduleData *Bundle = nullptr;
  bool ReSchedule = false;
  LLVM_DEBUG(dbgs() << "SLP:  bundle: ");
  for (int idx = 0; idx < VL.size(); idx++)
  {
    LLVM_DEBUG(dbgs() << *VL[0] << " , ");
  }
  LLVM_DEBUG(dbgs() << "\n");
  // Make sure that the scheduling region contains all
  // instructions of the bundle.
  for (Value *V : VL)
  {
    if (!extendSchedulingRegion(V))
      return false;
  }

  for (Value *V : VL)
  {
    ScheduleData *BundleMember = getScheduleData(V);
    assert(BundleMember &&
           "no ScheduleData for bundle member (maybe not in same basic block)");
    if (BundleMember->IsScheduled)
    {
      // A bundle member was scheduled as single instruction before and now
      // needs to be scheduled as part of the bundle. We just get rid of the
      // existing schedule.
      LLVM_DEBUG(dbgs() << "SLP:  reset schedule because " << *BundleMember << " was already scheduled\n");
      ReSchedule = true;
    }
    assert(BundleMember->isSchedulingEntity() &&
           "bundle member already part of other bundle");
    if (PrevInBundle)
    {
      PrevInBundle->NextInBundle = BundleMember;
    }
    else
    {
      Bundle = BundleMember;
    }
    BundleMember->UnscheduledDepsInBundle = 0;
    Bundle->UnscheduledDepsInBundle += BundleMember->UnscheduledDeps;

    // Group the instructions to a bundle.
    BundleMember->FirstInBundle = Bundle;
    PrevInBundle = BundleMember;
  }
  if (ScheduleEnd != OldScheduleEnd)
  {
    // The scheduling region got new instructions at the lower end (or it is a
    // new region for the first bundle). This makes it necessary to
    // recalculate all dependencies.
    // It is seldom that this needs to be done a second time after adding the
    // initial bundle to the region.
    for (auto *I = ScheduleStart; I != ScheduleEnd; I = I->getNextNode())
    {
      ScheduleData *SD = getScheduleData(I);
      SD->clearDependencies();
    }
    ReSchedule = true;
  }
  if (ReSchedule)
  {
    resetSchedule();
    initialFillReadyList(ReadyInsts);
  }

  LLVM_DEBUG(dbgs() << "SLP: try schedule bundle " << *Bundle << " in block "
                      << BB->getName() << "\n");

  calculateDependencies(Bundle, true, SLP);

  // Now try to schedule the new bundle. As soon as the bundle is "ready" it
  // means that there are no cyclic dependencies and we can schedule it.
  // Note that's important that we don't "schedule" the bundle yet (see
  // cancelScheduling).
  LLVM_DEBUG(dbgs() << "bundle is ready: " << Bundle->isReady() << "\n");
  while (!Bundle->isReady() && !ReadyInsts.empty())
  {

    ScheduleData *pickedSD = ReadyInsts.back();
    ReadyInsts.pop_back();
    //LLVM_DEBUG(dbgs() <<"isSchedulingEntity: "<< pickedSD->isSchedulingEntity()<<"isReady: "<< pickedSD->isReady()<<"\n");
    if (pickedSD->isSchedulingEntity() && pickedSD->isReady())
    {
      //LLVM_DEBUG(dbgs() <<"in the schedule loop\n");
      schedule(pickedSD, ReadyInsts);
    }
  }
  if (!Bundle->isReady())
  {
    LLVM_DEBUG(dbgs() << "UnscheduledDepsInBundle: " << Bundle->UnscheduledDepsInBundle << "\n");
    cancelScheduling(VL);
    return false;
  }
  return true;
}

void BoUpSLP::BlockScheduling::cancelScheduling(ArrayRef<Value *> VL)
{
  if (isa<PHINode>(VL[0]))
    return;

  ScheduleData *Bundle = getScheduleData(VL[0]);
  LLVM_DEBUG(dbgs() << "SLP:  cancel scheduling of " << *Bundle << "\n");
  assert(!Bundle->IsScheduled &&
         "Can't cancel bundle which is already scheduled");
  assert(Bundle->isSchedulingEntity() && Bundle->isPartOfBundle() &&
         "tried to unbundle something which is not a bundle");

  // Un-bundle: make single instructions out of the bundle.
  ScheduleData *BundleMember = Bundle;
  while (BundleMember)
  {
    assert(BundleMember->FirstInBundle == Bundle && "corrupt bundle links");
    BundleMember->FirstInBundle = BundleMember;
    ScheduleData *Next = BundleMember->NextInBundle;
    BundleMember->NextInBundle = nullptr;
    BundleMember->UnscheduledDepsInBundle = BundleMember->UnscheduledDeps;
    if (BundleMember->UnscheduledDepsInBundle == 0)
    {
      ReadyInsts.insert(BundleMember);
    }
    BundleMember = Next;
  }
}

bool BoUpSLP::BlockScheduling::extendSchedulingRegion(Value *V)
{
  if (getScheduleData(V))
    return true;
  Instruction *I = dyn_cast<Instruction>(V);
  assert(I && "bundle member must be an instruction");
  assert(!isa<PHINode>(I) && "phi nodes don't need to be scheduled");
  if (!ScheduleStart)
  {
    // It's the first instruction in the new region.
    initScheduleData(I, I->getNextNode(), nullptr, nullptr);
    ScheduleStart = I;
    ScheduleEnd = I->getNextNode();
    assert(ScheduleEnd && "tried to vectorize a TerminatorInst?");
    LLVM_DEBUG(dbgs() << "SLP:  initialize schedule region to " << *I << "\n");
    return true;
  }
  // Search up and down at the same time, because we don't know if the new
  // instruction is above or below the existing scheduling region.
  BasicBlock::reverse_iterator UpIter =
      ++ScheduleStart->getIterator().getReverse();
  BasicBlock::reverse_iterator UpperEnd = BB->rend();
  BasicBlock::iterator DownIter = ScheduleEnd->getIterator();
  BasicBlock::iterator LowerEnd = BB->end();
  for (;;)
  {
    if (++ScheduleRegionSize > ScheduleRegionSizeLimit)
    {
      LLVM_DEBUG(dbgs() << "SLP:  exceeded schedule region size limit\n");
      return false;
    }

    if (UpIter != UpperEnd)
    {
      if (&*UpIter == I)
      {
        initScheduleData(I, ScheduleStart, nullptr, FirstLoadStoreInRegion);
        ScheduleStart = I;
        LLVM_DEBUG(dbgs() << "SLP:  extend schedule region start to " << *I << "\n");
        return true;
      }
      UpIter++;
    }
    if (DownIter != LowerEnd)
    {
      if (&*DownIter == I)
      {
        initScheduleData(ScheduleEnd, I->getNextNode(), LastLoadStoreInRegion,
                         nullptr);
        ScheduleEnd = I->getNextNode();
        assert(ScheduleEnd && "tried to vectorize a TerminatorInst?");
        LLVM_DEBUG(dbgs() << "SLP:  extend schedule region end to " << *I << "\n");
        return true;
      }
      DownIter++;
    }
    assert((UpIter != UpperEnd || DownIter != LowerEnd) &&
           "instruction not found in block");
  }
  return true;
}

void BoUpSLP::BlockScheduling::initScheduleData(Instruction *FromI,
                                                Instruction *ToI,
                                                ScheduleData *PrevLoadStore,
                                                ScheduleData *NextLoadStore)
{
  ScheduleData *CurrentLoadStore = PrevLoadStore;
  for (Instruction *I = FromI; I != ToI; I = I->getNextNode())
  {
    ScheduleData *SD = ScheduleDataMap[I];
    if (!SD)
    {
      // Allocate a new ScheduleData for the instruction.
      if (ChunkPos >= ChunkSize)
      {
        ScheduleDataChunks.push_back(
            llvm::make_unique<ScheduleData[]>(ChunkSize));
        ChunkPos = 0;
      }
      SD = &(ScheduleDataChunks.back()[ChunkPos++]);
      ScheduleDataMap[I] = SD;
      SD->Inst = I;
    }
    assert(!isInSchedulingRegion(SD) &&
           "new ScheduleData already in scheduling region");
    SD->init(SchedulingRegionID);

    if (I->mayReadOrWriteMemory())
    {
      // Update the linked list of memory accessing instructions.
      if (CurrentLoadStore)
      {
        CurrentLoadStore->NextLoadStore = SD;
      }
      else
      {
        FirstLoadStoreInRegion = SD;
      }
      CurrentLoadStore = SD;
    }
  }
  if (NextLoadStore)
  {
    if (CurrentLoadStore)
      CurrentLoadStore->NextLoadStore = NextLoadStore;
  }
  else
  {
    LastLoadStoreInRegion = CurrentLoadStore;
  }
}

void BoUpSLP::BlockScheduling::calculateDependencies(ScheduleData *SD,
                                                     bool InsertInReadyList,
                                                     BoUpSLP *SLP)
{
  LLVM_DEBUG(dbgs() << "MYSLP in calculate Dependancies" << *SD << "\n");
  assert(SD->isSchedulingEntity());

  SmallVector<ScheduleData *, 10> WorkList;
  WorkList.push_back(SD);

  while (!WorkList.empty())
  {
    ScheduleData *SD = WorkList.back();
    WorkList.pop_back();

    ScheduleData *BundleMember = SD;
    while (BundleMember)
    {
      assert(isInSchedulingRegion(BundleMember));
      if (!BundleMember->hasValidDependencies())
      {
        LLVM_DEBUG(dbgs() << "SLP:       update deps of " << *BundleMember << "\n");
        BundleMember->Dependencies = 0;
        BundleMember->resetUnscheduledDeps();

        // Handle def-use chain dependencies.
        for (User *U : BundleMember->Inst->users())
        {
          if (isa<Instruction>(U))
          {
            ScheduleData *UseSD = getScheduleData(U);
            if (UseSD && isInSchedulingRegion(UseSD->FirstInBundle))
            {
              BundleMember->Dependencies++;
              ScheduleData *DestBundle = UseSD->FirstInBundle;
              if (!DestBundle->IsScheduled)
              {
                BundleMember->incrementUnscheduledDeps(1);
              }
              if (!DestBundle->hasValidDependencies())
              {
                LLVM_DEBUG(dbgs() << "MYSLP Worklist pushback1 " << *DestBundle << "\n");
                WorkList.push_back(DestBundle);
              }
            }
          }
          else
          {
            // I'm not sure if this can ever happen. But we need to be safe.
            // This lets the instruction/bundle never be scheduled and
            // eventually disable vectorization.
            BundleMember->Dependencies++;
            BundleMember->incrementUnscheduledDeps(1);
          }
        }

        // Handle the memory dependencies.
        ScheduleData *DepDest = BundleMember->NextLoadStore;

        if (DepDest)
        {
          LLVM_DEBUG(dbgs() << "MYSLP MemDEP in calculate Dependancies" << *DepDest << "\n");
          Instruction *SrcInst = BundleMember->Inst;
          MemoryLocation SrcLoc = getLocation(SrcInst, SLP->AA);
          bool SrcMayWrite = BundleMember->Inst->mayWriteToMemory();
          unsigned numAliased = 0;
          unsigned DistToSrc = 1;

          while (DepDest)
          {
            assert(isInSchedulingRegion(DepDest));

            // We have two limits to reduce the complexity:
            // 1) AliasedCheckLimit: It's a small limit to reduce calls to
            //    SLP->isAliased (which is the expensive part in this loop).
            // 2) MaxMemDepDistance: It's for very large blocks and it aborts
            //    the whole loop (even if the loop is fast, it's quadratic).
            //    It's important for the loop break condition (see below) to
            //    check this limit even between two read-only instructions.
            LLVM_DEBUG(dbgs() << "disttosrc (" << DistToSrc << ") mayWriteToMemory (" << DepDest->Inst->mayWriteToMemory() << ") numAliased (" << numAliased << ") isAliased (" << SLP->isAliased(SrcLoc, SrcInst, DepDest->Inst) << "\n");
            if (DistToSrc >= MaxMemDepDistance ||
                ((SrcMayWrite || DepDest->Inst->mayWriteToMemory()) &&
                 (numAliased >= AliasedCheckLimit ||
                  SLP->isAliased(SrcLoc, SrcInst, DepDest->Inst))))
            {

              // We increment the counter only if the locations are aliased
              // (instead of counting all alias checks). This gives a better
              // balance between reduced runtime and accurate dependencies.
              numAliased++;

              DepDest->MemoryDependencies.push_back(BundleMember);
              BundleMember->Dependencies++;
              ScheduleData *DestBundle = DepDest->FirstInBundle;
              if (!DestBundle->IsScheduled)
              {
                BundleMember->incrementUnscheduledDeps(1);
              }
              LLVM_DEBUG(dbgs() << "MYSLP before Worklist pushback ");
              if (!DestBundle->hasValidDependencies())
              {
                LLVM_DEBUG(dbgs() << "MYSLP Worklist pushback " << *DestBundle << "\n");
                WorkList.push_back(DestBundle);
              }
            }
            DepDest = DepDest->NextLoadStore;

            // Example, explaining the loop break condition: Let's assume our
            // starting instruction is i0 and MaxMemDepDistance = 3.
            //
            //                      +--------v--v--v
            //             i0,i1,i2,i3,i4,i5,i6,i7,i8
            //             +--------^--^--^
            //
            // MaxMemDepDistance let us stop alias-checking at i3 and we add
            // dependencies from i0 to i3,i4,.. (even if they are not aliased).
            // Previously we already added dependencies from i3 to i6,i7,i8
            // (because of MaxMemDepDistance). As we added a dependency from
            // i0 to i3, we have transitive dependencies from i0 to i6,i7,i8
            // and we can abort this loop at i6.
            if (DistToSrc >= 2 * MaxMemDepDistance)
              break;
            DistToSrc++;
          }
        }
      }
      BundleMember = BundleMember->NextInBundle;
    }
    if (InsertInReadyList && SD->isReady())
    {
      ReadyInsts.push_back(SD);
      LLVM_DEBUG(dbgs() << "SLP:     gets ready on update: " << *SD->Inst << "\n");
    }
  }
}

void BoUpSLP::setInsertPointAfterBundle(ArrayRef<Value *> VL)
{
  Instruction *VL0 = cast<Instruction>(VL[0]);
  // BasicBlock::iterator NextInst = VL0;
  BasicBlock::iterator NextInst(VL0);
  ++NextInst;
  Builder.SetInsertPoint(VL0->getParent(), NextInst);
  Builder.SetCurrentDebugLocation(VL0->getDebugLoc());
}

void BoUpSLP::BlockScheduling::resetSchedule()
{
  assert(ScheduleStart &&
         "tried to reset schedule on block which has not been scheduled");
  for (Instruction *I = ScheduleStart; I != ScheduleEnd; I = I->getNextNode())
  {
    ScheduleData *SD = getScheduleData(I);
    assert(isInSchedulingRegion(SD));
    SD->IsScheduled = false;
    SD->resetUnscheduledDeps();
  }
  ReadyInsts.clear();
}

void BoUpSLP::scheduleBlock(BlockScheduling *BS)
{

  if (!BS->ScheduleStart)
    return;

  LLVM_DEBUG(dbgs() << "SLP: schedule block " << BS->BB->getName() << "\n");

  BS->resetSchedule();

  // For the real scheduling we use a more sophisticated ready-list: it is
  // sorted by the original instruction location. This lets the final schedule
  // be as  close as possible to the original instruction order.
  struct ScheduleDataCompare
  {
    bool operator()(ScheduleData *SD1, ScheduleData *SD2)
    {
      return SD2->SchedulingPriority < SD1->SchedulingPriority;
    }
  };
  std::set<ScheduleData *, ScheduleDataCompare> ReadyInsts;

  // Ensure that all dependency data is updated and fill the ready-list with
  // initial instructions.
  int Idx = 0;
  int NumToSchedule = 0;
  for (auto *I = BS->ScheduleStart; I != BS->ScheduleEnd;
       I = I->getNextNode())
  {
    ScheduleData *SD = BS->getScheduleData(I);
    assert(
        SD->isPartOfBundle() == (ScalarToTreeEntry.count(SD->Inst) != 0) &&
        "scheduler and vectorizer have different opinion on what is a bundle");
    SD->FirstInBundle->SchedulingPriority = Idx++;
    if (SD->isSchedulingEntity())
    {
      BS->calculateDependencies(SD, false, this);
      NumToSchedule++;
    }
  }
  BS->initialFillReadyList(ReadyInsts);

  Instruction *LastScheduledInst = BS->ScheduleEnd;

  // Do the "real" scheduling.
  while (!ReadyInsts.empty())
  {
    ScheduleData *picked = *ReadyInsts.begin();
    ReadyInsts.erase(ReadyInsts.begin());

    // Move the scheduled instruction(s) to their dedicated places, if not
    // there yet.
    ScheduleData *BundleMember = picked;
    while (BundleMember)
    {
      Instruction *pickedInst = BundleMember->Inst;
      if (LastScheduledInst->getNextNode() != pickedInst)
      {
        BS->BB->getInstList().remove(pickedInst);
        BS->BB->getInstList().insert(LastScheduledInst->getIterator(),
                                     pickedInst);
      }
      LastScheduledInst = pickedInst;
      BundleMember = BundleMember->NextInBundle;
    }

    BS->schedule(picked, ReadyInsts);
    NumToSchedule--;
  }
  assert(NumToSchedule == 0 && "could not schedule all instructions");

  // Avoid duplicate scheduling of the block.
  BS->ScheduleStart = nullptr;
}

Value *BoUpSLP::alreadyVectorized(ArrayRef<Value *> VL) const
{
  SmallDenseMap<Value *, int>::const_iterator Entry = ScalarToTreeEntry.find(VL[0]);
  if (Entry != ScalarToTreeEntry.end())
  {
    int Idx = Entry->second;
    const TreeEntry *En = &VectorizableTree[Idx];
    if (En->isSame(VL) && En->VectorizedValue)
      return En->VectorizedValue;
  }
  return nullptr;
}
