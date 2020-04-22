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
static MemoryLocation getLocation(Instruction *I, AliasAnalysis *AA) {
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return MemoryLocation::get(SI);
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return MemoryLocation::get(LI);
  return MemoryLocation();
}

/// \returns True if the instruction is not a volatile or atomic load/store.
static bool isSimple(Instruction *I) {
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
static bool isValidElementType(Type *Ty) {
  return VectorType::isValidElementType(Ty) && !Ty->isX86_FP80Ty() &&
         !Ty->isPPC_FP128Ty();
}

/// \returns The opcode if all of the Instructions in \p VL have the same
/// opcode, or zero.
static unsigned getSameOpcode(ArrayRef<Value *> VL) {
  Instruction *I0 = dyn_cast<Instruction>(VL[0]);
  if (!I0)
    return 0;
  unsigned Opcode = I0->getOpcode();
  for (int i = 1, e = VL.size(); i < e; i++) {
    Instruction *I = dyn_cast<Instruction>(VL[i]);
    if (!I || Opcode != I->getOpcode()) {
      // if (canCombineAsAltInst(Opcode) && i == 1)
      //   return isAltInst(VL);
      return 0;
    }
  }
  return Opcode;
}

//TODO: remove this llvm 8.0.1 InstructionState
// /// Main data required for vectorization of instructions.
// struct InstructionsState {
//   /// The very first instruction in the list with the main opcode.
//   Value *OpValue = nullptr;

//   /// The main/alternate instruction.
//   Instruction *MainOp = nullptr;
//   //Instruction *AltOp = nullptr;

//   /// The main/alternate opcodes for the list of instructions.
//   unsigned getOpcode() const {
//     return MainOp ? MainOp->getOpcode() : 0;
//   }

//   // unsigned getAltOpcode() const {
//   //   return AltOp ? AltOp->getOpcode() : 0;
//   // }

//   /// Some of the instructions in the list have alternate opcodes.
//   // bool isAltShuffle() const { return getOpcode() != getAltOpcode(); }

//   // bool isOpcodeOrAlt(Instruction *I) const {
//   //   unsigned CheckedOpcode = I->getOpcode();
//   //   return getOpcode() == CheckedOpcode || getAltOpcode() == CheckedOpcode;
//   // }

//   bool isOpcode(Instruction *I) const{
//     unsigned CheckedOpcode = I->getOpcode();
//     return getOpcode() == CheckedOpcode;
//   }

//   InstructionsState() = delete;
//   InstructionsState(Value *OpValue, Instruction *MainOp)//, Instruction *AltOp)
//       : OpValue(OpValue), MainOp(MainOp){}//, AltOp(AltOp) {}
// };


// /// \returns analysis of the Instructions in \p VL described in
// /// InstructionsState, the Opcode that we suppose the whole list
// /// could be vectorized even if its structure is diverse.
// static InstructionsState getSameOpcode(ArrayRef<Value *> VL,
//                                        unsigned BaseIndex = 0) {
//   // Make sure these are all Instructions.
//   if (llvm::any_of(VL, [](Value *V) { return !isa<Instruction>(V); }))
//     return InstructionsState(VL[BaseIndex], nullptr);//, nullptr);

//   bool IsCastOp = isa<CastInst>(VL[BaseIndex]);
//   bool IsBinOp = isa<BinaryOperator>(VL[BaseIndex]);
//   unsigned Opcode = cast<Instruction>(VL[BaseIndex])->getOpcode();
//   //unsigned AltOpcode = Opcode;
//   //unsigned AltIndex = BaseIndex;

//   // Check for one alternate opcode from another BinaryOperator.
//   // TODO - generalize to support all operators (types, calls etc.).
//   for (int Cnt = 0, E = VL.size(); Cnt < E; Cnt++) {
//     unsigned InstOpcode = cast<Instruction>(VL[Cnt])->getOpcode();
//     if (IsBinOp && isa<BinaryOperator>(VL[Cnt])) {
//       if (InstOpcode == Opcode)// || InstOpcode == AltOpcode)
//         continue;
//       // if (Opcode == AltOpcode) {
//       //   AltOpcode = InstOpcode;
//       //   AltIndex = Cnt;
//       //   continue;
//       // }
//     } else if (IsCastOp && isa<CastInst>(VL[Cnt])) {
//       Type *Ty0 = cast<Instruction>(VL[BaseIndex])->getOperand(0)->getType();
//       Type *Ty1 = cast<Instruction>(VL[Cnt])->getOperand(0)->getType();
//       if (Ty0 == Ty1) {
//         if (InstOpcode == Opcode)// || InstOpcode == AltOpcode)
//           continue;
//         // if (Opcode == AltOpcode) {
//         //   AltOpcode = InstOpcode;
//         //   AltIndex = Cnt;
//         //   continue;
//        // }
//       }
//     } else if (InstOpcode == Opcode)// || InstOpcode == AltOpcode)
//       continue;
//     return InstructionsState(VL[BaseIndex], nullptr);//, nullptr);
//   }

//   return InstructionsState(VL[BaseIndex], cast<Instruction>(VL[BaseIndex]));//,
//                           //cast<Instruction>(VL[BaseIndex])); //cast<Instruction>(VL[AltIndex]));
// }


/// \returns the parent basic block if all of the instructions in \p VL
/// are in the same block or null otherwise.
static BasicBlock *getSameBlock(ArrayRef<Value *> VL) {
  Instruction *I0 = dyn_cast<Instruction>(VL[0]);
  if (!I0)
    return nullptr;
  BasicBlock *BB = I0->getParent();
  for (int i = 1, e = VL.size(); i < e; i++) {
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
static Type* getSameType(ArrayRef<Value *> VL) {
  Type *Ty = VL[0]->getType();
  for (int i = 1, e = VL.size(); i < e; i++)
    if (VL[i]->getType() != Ty)
      return nullptr;

  return Ty;
}

/// \returns True if the ExtractElement instructions in VL can be vectorized
/// to use the original vector.
static bool CanReuseExtract(ArrayRef<Value *> VL) {
  assert(Instruction::ExtractElement == getSameOpcode(VL) && "Invalid opcode");
  // Check if all of the extracts come from the same vector and from the
  // correct offset.
  Value *VL0 = VL[0];
  ExtractElementInst *E0 = cast<ExtractElementInst>(VL0);
  Value *Vec = E0->getOperand(0);

  // We have to extract from the same vector type.
  unsigned NElts = Vec->getType()->getVectorNumElements();

  if (NElts != VL.size())
    return false;

  // Check that all of the indices extract from the correct offset.
  ConstantInt *CI = dyn_cast<ConstantInt>(E0->getOperand(1));
  if (!CI || CI->getZExtValue())
    return false;

  for (unsigned i = 1, e = VL.size(); i < e; ++i) {
    ExtractElementInst *E = cast<ExtractElementInst>(VL[i]);
    ConstantInt *CI = dyn_cast<ConstantInt>(E->getOperand(1));

    if (!CI || CI->getZExtValue() != i || E->getOperand(0) != Vec)
      return false;
  }

  return true;
}

/// \returns True if all of the values in \p VL are identical.
static bool isSplat(ArrayRef<Value *> VL) {
  for (unsigned i = 1, e = VL.size(); i < e; ++i)
    if (VL[i] != VL[0])
      return false;
  return true;
}

static void reorderInputsAccordingToOpcode(ArrayRef<Value *> VL,
                                           SmallVectorImpl<Value *> &Left,
                                           SmallVectorImpl<Value *> &Right) {

  SmallVector<Value *, 16> OrigLeft, OrigRight;

  bool AllSameOpcodeLeft = true;
  bool AllSameOpcodeRight = true;
  for (unsigned i = 0, e = VL.size(); i != e; ++i) {
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

    if (i && AllSameOpcodeLeft) {
      if(Instruction *P0 = dyn_cast<Instruction>(OrigLeft[i-1])) {
        if(P0->getOpcode() != I0->getOpcode())
          AllSameOpcodeLeft = false;
      } else
        AllSameOpcodeLeft = false;
    }
    if (i && AllSameOpcodeRight) {
      if(Instruction *P1 = dyn_cast<Instruction>(OrigRight[i-1])) {
        if(P1->getOpcode() != I1->getOpcode())
          AllSameOpcodeRight = false;
      } else
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
    if (I0 && I1) {
       if(!i && I0->getOpcode() > I1->getOpcode()) {
         Left.push_back(I1);
         Right.push_back(I0);
       } else if (i && I0->getOpcode() > I1->getOpcode() && Right[i-1] != I1) {
         // Try not to destroy a broad cast for no apparent benefit.
         Left.push_back(I1);
         Right.push_back(I0);
       } else if (i && I0->getOpcode() == I1->getOpcode() && Right[i-1] ==  I0) {
         // Try preserve broadcasts.
         Left.push_back(I1);
         Right.push_back(I0);
       } else if (i && I0->getOpcode() == I1->getOpcode() && Left[i-1] == I1) {
         // Try preserve broadcasts.
         Left.push_back(I1);
         Right.push_back(I0);
       } else {
         Left.push_back(I0);
         Right.push_back(I1);
       }
       continue;
    }
    // One opcode, put the instruction on the right.
    if (I0) {
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
      (AllSameOpcodeRight || AllSameOpcodeLeft)) {
    Left = OrigLeft;
    Right = OrigRight;
  }
}
