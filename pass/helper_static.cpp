#pragma once


static const unsigned RecursionMaxDepth = 12;

/// \returns the AA location that is being access by the instruction.
static MemoryLocation getLocation(Instruction *I, AliasAnalysis *AA) {
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return MemoryLocation::get(SI);
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return MemoryLocation::get(LI);
  return MemoryLocation();
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