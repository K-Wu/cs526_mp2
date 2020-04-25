#pragma once

static const unsigned RecursionMaxDepth = 12;
static const int ScheduleRegionSizeBudget = 100000;
static const int MinVectorRegSizeOption = 128;

// Limit the number of alias checks. The limit is chosen so that
// it has no negative effect on the llvm benchmarks.
static const unsigned AliasedCheckLimit = 10;

// Another limit for the alias checks: The maximum distance between load/store
// instructions where alias checks are done.
// This limit is useful for very large basic blocks.
static const unsigned MaxMemDepDistance = 160;

/// If the ScheduleRegionSizeBudget is exhausted, we allow small scheduling
/// regions to be handled.
static const int MinScheduleRegionSize = 16;

/// \returns the AA location that is being access by the instruction.
static MemoryLocation getLocation(Instruction *I, AliasAnalysis *AA)
{
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return MemoryLocation::get(SI);
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return MemoryLocation::get(LI);
  return MemoryLocation();
}

/// \returns True if the instruction is not a volatile or atomic load/store.
static bool isSimple(Instruction *I)
{
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->isSimple();
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isSimple();
  if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I))
    return !MI->isVolatile();
  return true;
}

/// Predicate for the element types that the SLP vectorizer supports.
///
/// The most important thing to filter here are types which are invalid in LLVM
/// vectors. We also filter target specific types which have absolutely no
/// meaningful vectorization path such as x86_fp80 and ppc_f128. This just
/// avoids spending time checking the cost model and realizing that they will
/// be inevitably scalarized.
static bool isValidElementType(Type *Ty)
{
  return VectorType::isValidElementType(Ty) && !Ty->isX86_FP80Ty() &&
         !Ty->isPPC_FP128Ty();
}

/// \returns The opcode if all of the Instructions in \p VL have the same
/// opcode, or zero.
static unsigned getSameOpcode(ArrayRef<Value *> VL)
{
  Instruction *I0 = dyn_cast<Instruction>(VL[0]);
  if (!I0)
    return 0;
  unsigned Opcode = I0->getOpcode();
  for (int i = 1, e = VL.size(); i < e; i++)
  {
    Instruction *I = dyn_cast<Instruction>(VL[i]);
    if (!I || Opcode != I->getOpcode())
    {
      // if (canCombineAsAltInst(Opcode) && i == 1)
      //   return isAltInst(VL);
      return 0;
    }
  }
  return Opcode;
}

/// \returns the parent basic block if all of the instructions in \p VL
/// are in the same block or null otherwise.
static BasicBlock *getSameBlock(ArrayRef<Value *> VL)
{
  Instruction *I0 = dyn_cast<Instruction>(VL[0]);
  if (!I0)
    return nullptr;
  BasicBlock *BB = I0->getParent();
  for (int i = 1, e = VL.size(); i < e; i++)
  {
    Instruction *I = dyn_cast<Instruction>(VL[i]);
    if (!I)
      return nullptr;

    if (BB != I->getParent())
      return nullptr;
  }
  return BB;
}

/// \returns The type that all of the values in \p VL have or null if there
/// are different types.
static Type *getSameType(ArrayRef<Value *> VL)
{
  Type *Ty = VL[0]->getType();
  for (int i = 1, e = VL.size(); i < e; i++)
    if (VL[i]->getType() != Ty)
      return nullptr;

  return Ty;
}

/// \returns True if Extract{Value,Element} instruction extracts element Idx.
static bool matchExtractIndex(Instruction *E, unsigned Idx, unsigned Opcode)
{
  assert(Opcode == Instruction::ExtractElement ||
         Opcode == Instruction::ExtractValue);
  if (Opcode == Instruction::ExtractElement)
  {
    ConstantInt *CI = dyn_cast<ConstantInt>(E->getOperand(1));
    return CI && CI->getZExtValue() == Idx;
  }
  else
  {
    ExtractValueInst *EI = cast<ExtractValueInst>(E);
    return EI->getNumIndices() == 1 && *EI->idx_begin() == Idx;
  }
}

static unsigned canMapToVector(Type *T, const DataLayout &DL, const TargetTransformInfo *TTI)
{
  int MaxVecRegSize = TTI->getRegisterBitWidth(true);
  int MinVecRegSize = TTI->getMinVectorRegisterBitWidth();
  unsigned N;
  Type *EltTy;
  auto *ST = dyn_cast<StructType>(T);
  if (ST)
  {
    N = ST->getNumElements();
    EltTy = *ST->element_begin();
  }
  else
  {
    N = cast<ArrayType>(T)->getNumElements();
    EltTy = cast<ArrayType>(T)->getElementType();
  }
  if (!isValidElementType(EltTy))
    return 0;
  uint64_t VTSize = DL.getTypeStoreSizeInBits(VectorType::get(EltTy, N));
  if (VTSize < MinVecRegSize || VTSize > MaxVecRegSize || VTSize != DL.getTypeStoreSizeInBits(T))
    return 0;
  if (ST)
  {
    // Check that struct is homogeneous.
    for (const auto *Ty : ST->elements())
      if (Ty != EltTy)
        return 0;
  }
  return N;
}

/// \returns True if the ExtractElement instructions in VL can be vectorized
/// to use the original vector.
static bool CanReuseExtract(ArrayRef<Value *> VL, unsigned Opcode, const TargetTransformInfo *TTI)
{
  assert(Opcode == Instruction::ExtractElement ||
         Opcode == Instruction::ExtractValue);
  assert(Opcode == getSameOpcode(VL) && "Invalid opcode");
  // Check if all of the extracts come from the same vector and from the
  // correct offset.
  Value *VL0 = VL[0];
  Instruction *E0 = cast<Instruction>(VL0);
  Value *Vec = E0->getOperand(0);

  // We have to extract from a vector/aggregate with the same number of elements.
  unsigned NElts;
  if (Opcode == Instruction::ExtractValue)
  {
    const DataLayout &DL = E0->getModule()->getDataLayout();
    NElts = canMapToVector(Vec->getType(), DL, TTI);
    if (!NElts)
      return false;
    // Check if load can be rewritten as load of vector.
    LoadInst *LI = dyn_cast<LoadInst>(Vec);
    if (!LI || !LI->isSimple() || !LI->hasNUses(VL.size()))
      return false;
  }
  else
  {
    NElts = Vec->getType()->getVectorNumElements();
  }

  if (NElts != VL.size())
    return false;

  // Check that all of the indices extract from the correct offset.
  if (!matchExtractIndex(E0, 0, Opcode))
    return false;

  for (unsigned i = 1, e = VL.size(); i < e; ++i)
  {
    Instruction *E = cast<Instruction>(VL[i]);
    if (!matchExtractIndex(E, i, Opcode))
      return false;
    if (E->getOperand(0) != Vec)
      return false;
  }

  return true;
}

/// \returns True if all of the values in \p VL are constants.
static bool allConstant(ArrayRef<Value *> VL)
{
  for (unsigned i = 0, e = VL.size(); i < e; ++i)
    if (!isa<Constant>(VL[i]))
      return false;
  return true;
}

/// \returns True if all of the values in \p VL are identical.
static bool isSplat(ArrayRef<Value *> VL)
{
  for (unsigned i = 1, e = VL.size(); i < e; ++i)
    if (VL[i] != VL[0])
      return false;
  return true;
}

static void reorderInputsAccordingToOpcode(ArrayRef<Value *> VL,
                                           SmallVectorImpl<Value *> &Left,
                                           SmallVectorImpl<Value *> &Right)
{

  SmallVector<Value *, 16> OrigLeft, OrigRight;

  bool AllSameOpcodeLeft = true;
  bool AllSameOpcodeRight = true;
  for (unsigned i = 0, e = VL.size(); i != e; ++i)
  {
    Instruction *I = cast<Instruction>(VL[i]);
    Value *V0 = I->getOperand(0);
    Value *V1 = I->getOperand(1);

    OrigLeft.push_back(V0);
    OrigRight.push_back(V1);

    Instruction *I0 = dyn_cast<Instruction>(V0);
    Instruction *I1 = dyn_cast<Instruction>(V1);

    // Check whether all operands on one side have the same opcode. In this case
    // we want to preserve the original order and not make things worse by
    // reordering.
    AllSameOpcodeLeft = I0;
    AllSameOpcodeRight = I1;

    if (i && AllSameOpcodeLeft)
    {
      if (Instruction *P0 = dyn_cast<Instruction>(OrigLeft[i - 1]))
      {
        if (P0->getOpcode() != I0->getOpcode())
          AllSameOpcodeLeft = false;
      }
      else
        AllSameOpcodeLeft = false;
    }
    if (i && AllSameOpcodeRight)
    {
      if (Instruction *P1 = dyn_cast<Instruction>(OrigRight[i - 1]))
      {
        if (P1->getOpcode() != I1->getOpcode())
          AllSameOpcodeRight = false;
      }
      else
        AllSameOpcodeRight = false;
    }

    // Sort two opcodes. In the code below we try to preserve the ability to use
    // broadcast of values instead of individual inserts.
    // vl1 = load
    // vl2 = phi
    // vr1 = load
    // vr2 = vr2
    //    = vl1 x vr1
    //    = vl2 x vr2
    // If we just sorted according to opcode we would leave the first line in
    // tact but we would swap vl2 with vr2 because opcode(phi) > opcode(load).
    //    = vl1 x vr1
    //    = vr2 x vl2
    // Because vr2 and vr1 are from the same load we loose the opportunity of a
    // broadcast for the packed right side in the backend: we have [vr1, vl2]
    // instead of [vr1, vr2=vr1].
    if (I0 && I1)
    {
      if (!i && I0->getOpcode() > I1->getOpcode())
      {
        Left.push_back(I1);
        Right.push_back(I0);
      }
      else if (i && I0->getOpcode() > I1->getOpcode() && Right[i - 1] != I1)
      {
        // Try not to destroy a broad cast for no apparent benefit.
        Left.push_back(I1);
        Right.push_back(I0);
      }
      else if (i && I0->getOpcode() == I1->getOpcode() && Right[i - 1] == I0)
      {
        // Try preserve broadcasts.
        Left.push_back(I1);
        Right.push_back(I0);
      }
      else if (i && I0->getOpcode() == I1->getOpcode() && Left[i - 1] == I1)
      {
        // Try preserve broadcasts.
        Left.push_back(I1);
        Right.push_back(I0);
      }
      else
      {
        Left.push_back(I0);
        Right.push_back(I1);
      }
      continue;
    }
    // One opcode, put the instruction on the right.
    if (I0)
    {
      Left.push_back(V1);
      Right.push_back(I0);
      continue;
    }
    Left.push_back(V0);
    Right.push_back(V1);
  }

  bool LeftBroadcast = isSplat(Left);
  bool RightBroadcast = isSplat(Right);

  // Don't reorder if the operands where good to begin with.
  if (!(LeftBroadcast || RightBroadcast) &&
      (AllSameOpcodeRight || AllSameOpcodeLeft))
  {
    Left = OrigLeft;
    Right = OrigRight;
  }
}

/// Get the intersection (logical and) of all of the potential IR flags
/// of each scalar operation (VL) that will be converted into a vector (I).
/// Flag set: NSW, NUW, exact, and all of fast-math.
static void propagateIRFlags(Value *I, ArrayRef<Value *> VL)
{
  if (auto *VecOp = dyn_cast<Instruction>(I))
  {
    if (auto *Intersection = dyn_cast<Instruction>(VL[0]))
    {
      // Intersection is initialized to the 0th scalar,
      // so start counting from index '1'.
      for (int i = 1, e = VL.size(); i < e; ++i)
      {
        if (auto *Scalar = dyn_cast<Instruction>(VL[i]))
          Intersection->andIRFlags(Scalar);
      }
      VecOp->copyIRFlags(Intersection);
    }
  }
}