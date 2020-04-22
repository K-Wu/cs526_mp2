#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Optional.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include <vector>
#include <unordered_map>
#include <list>

using namespace llvm;
#define DEBUGKWU
#ifdef DEBUGKWU
#define dbg_executes(...) \
  {                       \
    __VA_ARGS__           \
  }                       \
  while (0)
#else
#define dbg_executes(...) \
  {                       \
  }                       \
  while (0)
#endif

#define DEBUG_TYPE "slpvect-kdw"
#define SV_NAME "slpvect-kdw"

#include "helper_static.cpp"
//namespace
//{
#include "helper.cpp"

void BoUpSLP::buildTree_rec(ArrayRef<Value *> VL, unsigned Depth)
{
  dbg_executes(errs() << "MySLP buildtree_rec entry bundle: ";);
  for (int idx = 0; idx < VL.size(); idx++)
  {
    dbg_executes(errs() << " , " << *(VL[idx]););
  }
  dbg_executes(errs() << "\n";);
  //if recursion reaches max depth, stop
  if (Depth == RecursionMaxDepth)
  {
    dbg_executes(errs() << "MySLP: buildTree max depth\n";);
    newTreeEntry(VL, false);
    return;
  }

  //if not the same type or invalid vector element type, stop
  if (!getSameType(VL))
  {
    dbg_executes(errs() << "MySLP: buildTree not same type\n";);
    newTreeEntry(VL, false);
    return;
  }
  for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
  {
    if (StoreInst *SI = dyn_cast<StoreInst>(VL[idx_vl]))
    {
      //store instruction needs and only needs to check its store pointer
      if (!isValidElementType(SI->getPointerOperand()->getType()))
      {
        dbg_executes(errs() << "MySLP: buildTree store not valid type\n";);
        newTreeEntry(VL, false);
        return;
      }
    }
    else
    {
      if (!isValidElementType(VL[idx_vl]->getType()))
      {
        dbg_executes(errs() << "MySLP: buildTree not valid type\n";);
        newTreeEntry(VL, false);
        return;
      }
    }
  }

  //if the instructions are not in the same block or same OP stop
  unsigned OpCode = getSameOpcode(VL);
  BasicBlock *BB = getSameBlock(VL);
  if (!OpCode || !BB)
  {
    dbg_executes(errs() << "MySLP: buildTree not same block or same opcode\n";);
    newTreeEntry(VL, false);
    return;
  }

  // Check if this is a duplicate of another scalar in the tree.
  if (ScalarToTreeEntry.count(VL[0]))
  {
    TreeEntry *TE = &VectorizableTree[ScalarToTreeEntry[VL[0]]];
    for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
    {
      if (TE->Scalars[idx_vl] != VL[idx_vl])
      {
        newTreeEntry(VL, false); //partial overlap
        return;
      }
    }
    //equal. Don't create new TreeEntry as it overwrites the ScalarToTreeEntry entries.
    //TODO: vectorizeTree() recursion part needs to pass the ArrayRef<Value*> VL rather than the tree node and check whether the ArrayRef<Value*> is in the tree.
    return;
  }

  // check block reachability
  if (!DT->isReachableFromEntry(BB))
  {
    dbg_executes(errs() << "MySLP: buildTree not reachable block\n";);
    newTreeEntry(VL, false);
    return;
  }

  // check no duplication in this bundle
  std::set<Value *> uniqueVLElement;
  for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
  {
    if (uniqueVLElement.count(VL[idx_vl]))
    {
      dbg_executes(errs() << "MySLP: buildTree duplicate item in bundle\n";);
      newTreeEntry(VL, false);
      return;
    }
    uniqueVLElement.insert(VL[idx_vl]);
  }

  // check scheduling
  if (!BlocksSchedules.count(BB))
  {
    BlocksSchedules[BB] = llvm::make_unique<BlockScheduling>(BB);
  }
  auto &BSRef = BlocksSchedules[BB];
  auto &BS = *BSRef.get();

  dbg_executes(errs() << "WARNING: BS " << BS.BB << "\n";);
  if (!BS.tryScheduleBundle(VL, this))
  {
    //BS.cancelScheduling(VL);
    dbg_executes(errs() << "MySLP: buildTree cannot schedule\n";);
    newTreeEntry(VL, false); //cannot schedule
    return;
  }

  //cast the first instruction for use
  Instruction *VL0 = dyn_cast<Instruction>(VL[0]);
  if (!VL0)
  {
    dbg_executes(errs() << "shouldn't be here Instruction cast failed\n";);
    BS.cancelScheduling(VL);
    newTreeEntry(VL, false);
    return;
  }

  switch (OpCode)
  {
  case Instruction::Load:
  {
    //ensure simple and consecutive
    for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
    {
      if (!dyn_cast<LoadInst>(VL[idx_vl])->isSimple())
      {
        dbg_executes(errs() << "MySLP: not vectorized due to LoadInst is not simple\n";);
        BS.cancelScheduling(VL);
        newTreeEntry(VL, false);
        return;
      }
      if (idx_vl != VL.size() - 1)
      {
        if (!isConsecutiveAccess(VL[idx_vl], VL[idx_vl + 1]))
        {
          dbg_executes(errs() << "MySLP: not vectorized due to LoadInst does not satisfy isConsecutiveAccess\n";);
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false);
          return;
        }
      }
    }
    newTreeEntry(VL, true);
    // for (int idx_operand = 0; idx_operand < dyn_cast<LoadInst>(VL[0])->getNumOperands(); idx_operand++)
    // {
    //   std::vector<Value *> operands;
    //   for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
    //   {
    //     operands.push_back(dyn_cast<LoadInst>(VL[idx_vl])->getOperand(idx_operand));
    //   }
    //   buildTree_rec(operands, Depth + 1);
    // }
    return;
  }
  case Instruction::Store:
  {
    //ensure simple and consecutive
    for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
    {
      if (!dyn_cast<StoreInst>(VL[idx_vl])->isSimple())
      {
        dbg_executes(errs() << "MySLP: not vectorized due to StoreInst is not simple\n";);
        BS.cancelScheduling(VL);
        newTreeEntry(VL, false);
        return;
      }
      if (idx_vl != VL.size() - 1)
      {
        if (!isConsecutiveAccess(VL[idx_vl], VL[idx_vl + 1]))
        {
          dbg_executes(errs() << "MySLP: not vectorized due to StoreInst does not satisfy isConsecutiveAccess\n";);
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false);
          return;
        }
      }
    }
    newTreeEntry(VL, true);
    // for (int idx_operand = 0; idx_operand < dyn_cast<StoreInst>(VL[0])->getNumOperands(); idx_operand++)
    // {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<StoreInst>(VL[idx_vl])->getOperand(0));
      }
      buildTree_rec(operands, Depth + 1);
    //}
    return;
  }
  case Instruction::ICmp:
  case Instruction::FCmp:
  {
    //ensure the predicate is the same type and cond is the same type
    auto VL0Pred = dyn_cast<CmpInst>(VL[0])->getPredicate();
    Type *VL0CondType = dyn_cast<CmpInst>(VL[0])->getOperand(0)->getType();
    for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
    {
      if ((VL0Pred != dyn_cast<CmpInst>(VL[idx_vl])->getPredicate()) || (VL0CondType != dyn_cast<CmpInst>(VL[idx_vl])->getOperand(0)->getType()))
      {
        //not the same predicate or same type cond
        BS.cancelScheduling(VL);
        newTreeEntry(VL, false);
        return;
      }
    }
    newTreeEntry(VL, true);
    for (int idx_operand = 0; idx_operand < VL0->getNumOperands(); idx_operand++)
    {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<CmpInst>(VL[idx_vl])->getOperand(idx_operand));
      }
      buildTree_rec(operands, Depth + 1);
    }
    return;
  }
  case Instruction::Select:
  //BinaryOperators
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  // Logical operators (integer operands)
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  {
    //build for each operand
    newTreeEntry(VL, true);
    for (int idx_operand = 0; idx_operand < VL0->getNumOperands(); idx_operand++)
    {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<Instruction>(VL[idx_vl])->getOperand(idx_operand));
      }
      buildTree_rec(operands, Depth + 1);
    }
    return;
  }
  //conversion operators
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  {
    //ensure target type are the same. now we need to check the src type
    Type *SrcTy = VL0->getOperand(0)->getType();
    if (!isValidElementType(SrcTy))
    {
      BS.cancelScheduling(VL);
      newTreeEntry(VL, false);
      return;
    }
    for (int idx = 1; idx < VL.size(); idx++)
    {
      Type *curr_type = dyn_cast<Instruction>(VL[idx])->getType();
      if ((curr_type != SrcTy) || (!isValidElementType(curr_type)))
      { //not the same type or invalid vector type
        BS.cancelScheduling(VL);
        newTreeEntry(VL, false);
        return;
      }
    }
    newTreeEntry(VL, true);
    for (int idx_operand = 0; idx_operand < VL0->getNumOperands(); idx_operand++)
    {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<Instruction>(VL[idx_vl])->getOperand(idx_operand));
      }
      buildTree_rec(operands, Depth + 1);
    }
    return;
  }
  //do not deal with
  case Instruction::ExtractValue:
  case Instruction::ExtractElement:
  case Instruction::GetElementPtr:
  case Instruction::PHI:
  case Instruction::Call:
  case Instruction::ShuffleVector:
  {
    BS.cancelScheduling(VL);
    newTreeEntry(VL, false); //cannot schedule
    return;
  }
  default:
  { //TODO: what about alloca, atomic operations that bump into this scheme?
    llvm_unreachable("unexpected Opcode");
  }
  }
}

void BoUpSLP::buildTree(ArrayRef<Value *> Roots)
{
  dbg_executes(errs() << "WARNING: STOREVL: ";);
  for (auto siter = Roots.begin(); siter != Roots.end(); siter++)
  {
    dbg_executes(errs() << *siter << ", ";);
  }
  dbg_executes(errs() << "\n";);
  buildTree_rec(Roots, 0);
  for (int idx_te = 0; idx_te < VectorizableTree.size(); idx_te++)
  {
    TreeEntry *TE = &VectorizableTree[idx_te];
    if (TE->NeedToGather)
      continue; //don't need to extractelement for non-vectorizable leaves use as they are not vectorized.
    for (int idx2_scalar = 0; idx2_scalar < TE->Scalars.size(); idx2_scalar++)
    {
      for (User *U : TE->Scalars[idx2_scalar]->users())
      {
        if (!ScalarToTreeEntry[TE->Scalars[idx2_scalar]])
          ExternalUses.push_back(ExternalUser(TE->Scalars[idx2_scalar], U, idx2_scalar));
      }
    }
  }
}



Value *BoUpSLP::Gather(ArrayRef<Value *> VL, VectorType *Ty)
{
  // This function gathers a list of scalars into a vector by inserting some
  // InsertElement instructions. If a scalar is in the tree, it will be removed
  // finally. However, the new InsertElement instruction is a user of this scalar, so we need to add
  // an ExtractElement instruction before this InsertElement instruction.
  // To do this, we put create a record in ExternalUses
  // such that it will be handled later.

  // Note: before calling this function, insert point has to be set by the caller.

  // First create a new empty vector.
  Value *new_vector = UndefValue::get(Ty);
  for (int i = 0; i < VL.size(); i++)
  {
    auto scalar = VL[i];
    // Step 1, assemble the new vector by inserting InsertElement instructions
    new_vector = Builder.CreateInsertElement(new_vector, scalar, Builder.getInt32(i));
    Instruction *I = dyn_cast<Instruction>(new_vector);

    // TODO: optimizeGatherSequence()

    // Step 2, after such vectorization, the vectorized value Vec is outside the tree. The in the tree user of the scalar element in the vectorized value Vec needs to be satisfied by ExtractElement.
    //Add (User, Usee)=(Vec, VL[i]) to the ExternalUses. 
    if (ScalarToTreeEntry.count(scalar))
    {
      // Found the scalar in the tree.
      // Find the corresponding entry of the tree.
      TreeEntry *E = &VectorizableTree[ScalarToTreeEntry[scalar]];
      // TODO: put E directly into ExternalUser
      // Next, find the position of the scalar in E.
      for (int pos = 0; pos < E->Scalars.size(); pos++)
      {
        if (E->Scalars[pos] == scalar)
        {
          ExternalUses.push_back(ExternalUser(scalar, I, pos));
          break;
        }
      }
    }
  }

  return new_vector;
}

// Given a scalar value. Return the corresponding vector type.
VectorType *getVectorType(Value *scalar, unsigned int length)
{
  Type *type = scalar->getType();
  if (StoreInst *SI = dyn_cast<StoreInst>(scalar))
  {
    // If scalar is the ret of a store op, then we use the type of the value being stored.
    type = SI->getValueOperand()->getType();
  }
  VectorType *vector_type = VectorType::get(type, length);
  return vector_type;
}

// Vectorize a list of scalars. If they are in the tree (and in the same entry),
// vectorize the corresponding tree entry, otherwise,
// gather the scalars into a new vector.
// Note: before calling this function, insert point has to be set by the caller.
Value *BoUpSLP::vectorizeTree_rec(ArrayRef<Value *> VL)
{
  dbg_executes(errs() << "MySLP vectorizeTree_rec entry bundle: ";);
  for (int idx = 0; idx < VL.size(); idx++)
  {
    dbg_executes(errs() << " , " << *(VL[idx]););
  }
  dbg_executes(errs() << "\n";);
  auto scalar = VL[0];
  auto length = VL.size();

  // Case 1, scalars are in the tree.
  // TODO: merge common logic with alreadyVectorized()
  if (ScalarToTreeEntry.count(scalar))
  {
    // Found the scalar in the tree.
    // Find the corresponding entry of the tree.
    TreeEntry *E = &VectorizableTree[ScalarToTreeEntry[scalar]];
    //we have to check whether VL == E.Scalars exactly
    if (E->isSame(VL))
    {
      // VL == E.Scalars exactly, so we do not have to create a new vector
      // vectorize E direcly.
      return do_vectorizeTree_rec(E);
    }
  }
  //Case 2, scalars are not in the tree.
  // Create a new vector by gathering
  auto vector_type = getVectorType(scalar, length);
  return Gather(VL, vector_type);
}

// Vectorize a single tree entry if it has not been vectorized.
// Currently, this function only handles Binary, Load/Store, and GEP OPs.
Value *BoUpSLP::do_vectorizeTree_rec(TreeEntry *E)
{
  // Case 1, check if this entry has been vectorized.
  // If so, return directly.
  if (E->VectorizedValue)
  {
    return E->VectorizedValue;
  }

  Instruction *scalar = cast<Instruction>(E->Scalars[0]);
  auto vector_type = getVectorType(scalar, E->Scalars.size());

  // Case 2, If this entry is marked as NeedToGather, then it is easy. Just Gather it.
  if (E->NeedToGather)
  {
    setInsertPointAfterBundle(E->Scalars);
    return Gather(E->Scalars, vector_type);
  }

  // Case 3, otherwise, we vectorize this entry according to the operation.
  // BasicBlock *BB = scalar->getParent();
  // scheduleBlock(BB);

  unsigned Opcode = getSameOpcode(E->Scalars);

  // TODO: remove this after refactoring
  Instruction *VL0 = scalar;
  Type *ScalarTy = VL0->getType();
  VectorType *VecTy = vector_type;

  switch (Opcode)
  {
  // Several different cases.
  // The general idea is:
  // first we collect the operands of this instruction and vectorize it.
  // then convert this instruction into a vectorized version with the vectorized operands.
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  {
    // For binary operators. Operands: LHS and RHS
    // Step 1, collect operands
    ValueList LHS_scalars, RHS_scalars;
    if (isa<BinaryOperator>(VL0) && VL0->isCommutative())
    {
      //
      reorderInputsAccordingToOpcode(E->Scalars, LHS_scalars, RHS_scalars);
    }
    else
    {
      for (int i = 0; i < E->Scalars.size(); i++)
      {
        Instruction *I = cast<Instruction>(E->Scalars[i]);
        LHS_scalars.push_back(I->getOperand(0));
        RHS_scalars.push_back(I->getOperand(0));
      }
    }
    // Step 2, vecotrize operands
    setInsertPointAfterBundle(E->Scalars);
    auto LHS_vector = vectorizeTree_rec(LHS_scalars);
    auto RHS_vector = vectorizeTree_rec(RHS_scalars);

    if (alreadyVectorized(E->Scalars))
    {
      // it is possible that when vectorizing the operands, we also vectorized this entry.
      return alreadyVectorized(E->Scalars);
    }
    // Step 3, create a new Instruction with the vectorized operands.
    Value *V = Builder.CreateBinOp(cast<BinaryOperator>(scalar)->getOpcode(), LHS_vector, RHS_vector);
    E->VectorizedValue = V;

    if (Instruction *I = dyn_cast<Instruction>(V))
      return propagateMetadata(I, E->Scalars);

    return V;
  }
  case Instruction::Load:
  {
    // Load Instruction: Operands: addresses. For this one we don't have to vectorize the operand.
    setInsertPointAfterBundle(E->Scalars);
    LoadInst *LI = cast<LoadInst>(scalar);
    unsigned AS = LI->getPointerAddressSpace();
    // vector_type->getPointerTo(AS) return PtrType pointing to vector_type
    // By construction, scalar is the first element of this vector, so we can
    // use its address as the address of the vector.
    Value *vector_ptr = Builder.CreateBitCast(LI->getPointerOperand(), vector_type->getPointerTo(AS));

    unsigned Alignment = LI->getAlignment();

    // create a new Load instruction which loads a vector from the address of scalar.
    LI = Builder.CreateLoad(vector_ptr);
    if (!Alignment)
      Alignment = DL->getABITypeAlignment(LI->getPointerOperand()->getType());
    LI->setAlignment(Alignment);
    E->VectorizedValue = LI;
    return propagateMetadata(LI, E->Scalars);
  }
  case Instruction::Store:
  {
    // Store instruction: single operand
    StoreInst *SI = cast<StoreInst>(VL0);
    unsigned AS = SI->getPointerAddressSpace();
    unsigned Alignment = SI->getAlignment();
    if (!Alignment)
      Alignment = DL->getABITypeAlignment(SI->getPointerOperand()->getType());

    // Step 1, collect operands
    ValueList operand_scalars;
    for (int i = 0; i < E->Scalars.size(); i++)
    {
      auto *I = cast<StoreInst>(E->Scalars[i]);
      operand_scalars.push_back(I->getValueOperand());
    }

    // Step 2, vectorize operands
    setInsertPointAfterBundle(E->Scalars);
    Value *operand_vector = vectorizeTree_rec(operand_scalars);
    Value *vector_ptr = Builder.CreateBitCast(SI->getPointerOperand(), vector_type->getPointerTo(AS));

    SI = Builder.CreateStore(operand_vector, vector_ptr);
    SI->setAlignment(Alignment);
    E->VectorizedValue = SI;
    return propagateMetadata(SI, E->Scalars);
  }
  case Instruction::GetElementPtr:
  {
    llvm_unreachable("GEP not implemented"); 
    // GEP. number of operands varies
    setInsertPointAfterBundle(E->Scalars);

    // Step 1 and 2, collect and vectorize all operands.
    // collect and vectorize operand 0's
    ValueList op0_scalars;
    for (int i = 0; i < E->Scalars.size(); i++)
    {
      auto *I = cast<GetElementPtrInst>(E->Scalars[i]);
      op0_scalars.push_back(I->getOperand(0));
    }
    Value *op0_vector = vectorizeTree_rec(op0_scalars);

    // collect and vectorize other operands
    std::vector<Value *> other_operands_vector;
    for (int j = 0; j < cast<GetElementPtrInst>(E->Scalars[0])->getNumOperands() - 1; j++)
    {
      ValueList opj_scalars;
      for (int i = 0; i < E->Scalars.size(); i++)
      {
        auto *I = cast<GetElementPtrInst>(E->Scalars[i]);
        opj_scalars.push_back(I->getOperand(j + 1));
      }
      Value *opj_vector = vectorizeTree_rec(opj_scalars);
      other_operands_vector.push_back(opj_vector);
    }

    // Step 2, create  new GEP instruction
    Value *GEPI = Builder.CreateGEP(op0_vector, other_operands_vector);
    E->VectorizedValue = GEPI;

    if (Instruction *I = dyn_cast<Instruction>(GEPI))
      return propagateMetadata(I, E->Scalars);

    return GEPI;
  }
  // TODO: the following code is copied from the original file
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  {
    ValueList INVL;
    for (int i = 0, e = E->Scalars.size(); i < e; ++i)
      INVL.push_back(cast<Instruction>(E->Scalars[i])->getOperand(0));

    setInsertPointAfterBundle(E->Scalars);

    Value *InVec = vectorizeTree_rec(INVL);

    if (Value *V = alreadyVectorized(E->Scalars))
      return V;

    CastInst *CI = dyn_cast<CastInst>(VL0);
    Value *V = Builder.CreateCast(CI->getOpcode(), InVec, VecTy);
    E->VectorizedValue = V;
    return V;
  }
  case Instruction::FCmp:
  case Instruction::ICmp:
  {
    ValueList LHSV, RHSV;
    for (int i = 0, e = E->Scalars.size(); i < e; ++i)
    {
      LHSV.push_back(cast<Instruction>(E->Scalars[i])->getOperand(0));
      RHSV.push_back(cast<Instruction>(E->Scalars[i])->getOperand(1));
    }

    setInsertPointAfterBundle(E->Scalars);

    Value *L = vectorizeTree_rec(LHSV);
    Value *R = vectorizeTree_rec(RHSV);

    if (Value *V = alreadyVectorized(E->Scalars))
      return V;

    CmpInst::Predicate P0 = dyn_cast<CmpInst>(VL0)->getPredicate();
    Value *V;
    if (Opcode == Instruction::FCmp)
      V = Builder.CreateFCmp(P0, L, R);
    else
      V = Builder.CreateICmp(P0, L, R);

    E->VectorizedValue = V;
    return V;
  }
  case Instruction::Select:
  {
    ValueList TrueVec, FalseVec, CondVec;
    for (int i = 0, e = E->Scalars.size(); i < e; ++i)
    {
      CondVec.push_back(cast<Instruction>(E->Scalars[i])->getOperand(0));
      TrueVec.push_back(cast<Instruction>(E->Scalars[i])->getOperand(1));
      FalseVec.push_back(cast<Instruction>(E->Scalars[i])->getOperand(2));
    }

    setInsertPointAfterBundle(E->Scalars);

    Value *Cond = vectorizeTree_rec(CondVec);
    Value *True = vectorizeTree_rec(TrueVec);
    Value *False = vectorizeTree_rec(FalseVec);

    if (Value *V = alreadyVectorized(E->Scalars))
      return V;

    Value *V = Builder.CreateSelect(Cond, True, False);
    E->VectorizedValue = V;
    return V;
  }
  //not implemented
  case Instruction::Call:
  case Instruction::ShuffleVector:
  case Instruction::ExtractValue:
  case Instruction::ExtractElement:
  case Instruction::PHI:
  default:
    llvm_unreachable("unknown inst");
  }
  return nullptr;
}

// Vectorize the tree from root recursively.
Value *BoUpSLP::vectorizeTree()
{
  // All blocks must be scheduled before any instructions are inserted.
  for (auto &BSIter : BlocksSchedules)
  {
    scheduleBlock(BSIter.second.get());
  }

  // Step 1, recursively vectorize starting from the root.
  Builder.SetInsertPoint(&F->getEntryBlock().front());
  do_vectorizeTree_rec(&VectorizableTree[0]);


  // Step 2, we need to ExtractElement from the designated lane of the vectorized value for uses external to the tree.
  for (UserList::iterator it = ExternalUses.begin(), e = ExternalUses.end();
       it != e; ++it)
  {
    Value *Scalar = it->Scalar;
    llvm::User *User = it->User;

    // Skip users that we already RAUW. This happens when one instruction
    // has multiple uses of the same value.
    if (std::find(Scalar->user_begin(), Scalar->user_end(), User) ==
        Scalar->user_end())
      continue;
    assert(ScalarToTreeEntry.count(Scalar) && "Invalid scalar");

    int Idx = ScalarToTreeEntry[Scalar];
    TreeEntry *E = &VectorizableTree[Idx];
    assert(!E->NeedToGather && "Extracting from a gather list");

    Value *Vec = E->VectorizedValue;
    assert(Vec && "Can't find vectorizable value");

    Value *Lane = Builder.getInt32(it->Lane);
    // Generate extracts for out-of-tree users.
    // Find the insertion point for the extractelement lane.
    if (isa<Instruction>(Vec))
    {
      if (PHINode *PH = dyn_cast<PHINode>(User))
      {
        for (int i = 0, e = PH->getNumIncomingValues(); i != e; ++i)
        {
          if (PH->getIncomingValue(i) == Scalar)
          {
            Builder.SetInsertPoint(PH->getIncomingBlock(i)->getTerminator());
            Value *Ex = Builder.CreateExtractElement(Vec, Lane);
            PH->setOperand(i, Ex);
          }
        }
      }
      else
      {
        Builder.SetInsertPoint(cast<Instruction>(User));
        Value *Ex = Builder.CreateExtractElement(Vec, Lane);
        User->replaceUsesOfWith(Scalar, Ex);
      }
    }
    else
    {
      Builder.SetInsertPoint(&F->getEntryBlock().front());
      Value *Ex = Builder.CreateExtractElement(Vec, Lane);
      User->replaceUsesOfWith(Scalar, Ex);
    }

  }

  // Step 3, replace all the uses of scalars with undef such that these uses will be removed
  for (int EIdx = 0, EE = VectorizableTree.size(); EIdx < EE; ++EIdx)
  {
    TreeEntry *Entry = &VectorizableTree[EIdx];

    // For each lane:
    for (int Lane = 0, LE = Entry->Scalars.size(); Lane != LE; ++Lane)
    {
      Value *Scalar = Entry->Scalars[Lane];
      // Since the users of gathered values have already been replaced with
      // ExtractElement instructions, we should skip those cases.
      if (Entry->NeedToGather)
        continue;

      assert(Entry->VectorizedValue && "Can't find vectorizable value");

      Type *Ty = Scalar->getType();
      if (!Ty->isVoidTy())
      {
        Value *Undef = UndefValue::get(Ty);
        Scalar->replaceAllUsesWith(Undef);
      }
      cast<Instruction>(Scalar)->eraseFromParent();
    }
  }

  // for (auto &BN : BlocksNumbers)
  //   BN.second.forget();

  Builder.ClearInsertionPoint();

  return VectorizableTree[0].VectorizedValue;
}

std::list<std::vector<Value *>> collectStores(BasicBlock *BB, BoUpSLP &R)
{
  std::list<std::vector<Value *>> storePacks;

  std::vector<Value *> storeInsts;
  std::vector<Value *> heads;
  std::unordered_map<Value *, Value *> tail_to_head_map;
  std::unordered_map<Value *, Value *> head_to_tail_map;
  std::unordered_map<Value *, bool> visited_heads;

  //find out all the store instructions in this basic block
  for (BasicBlock::iterator iter = BB->begin(); iter != BB->end(); iter++)
  {
    if (StoreInst *SI = dyn_cast<StoreInst>(iter))
    {
      storeInsts.push_back(SI);
    }
  }

  //O(N^2) brute force find consecutive store pairs
  for (int idx1 = 0; idx1 < storeInsts.size(); idx1++)
  {
    for (int idx2 = 0; idx2 < storeInsts.size(); idx2++)
    {
      if (idx1 == idx2)
        continue;
      if (R.isConsecutiveAccess(storeInsts[idx1], storeInsts[idx2]))
      {
        heads.push_back(storeInsts[idx1]);
        head_to_tail_map[storeInsts[idx1]] = storeInsts[idx2];
        tail_to_head_map[storeInsts[idx2]] = storeInsts[idx1];
        visited_heads[storeInsts[idx1]] = false;
      }
    }
  }

  //connect consecutive store pairs into packs
  for (int idx = 0; idx < heads.size(); idx++)
  {
    if (visited_heads[heads[idx]])
      continue;
    //find the very beginning store instruction
    Value *beginInst = heads[idx];
    while (tail_to_head_map.count(beginInst))
      beginInst = tail_to_head_map[beginInst];
    if (!beginInst)
      dbg_executes(errs() << "FUCK!\n";);
    std::vector<Value *> storePack;
    storePack.push_back(beginInst);
    visited_heads[beginInst] = true;
    while (head_to_tail_map.count(beginInst))
    {
      beginInst = head_to_tail_map[beginInst];
      storePack.push_back(beginInst);
      visited_heads[beginInst] = true;
    }
    dbg_executes(errs() << "WARNING: STOREVAL IN COLLECTSTORES: ";);
    for (int idx1 = 0; idx1 < storePack.size(); idx1++)
    {
      dbg_executes(errs() << storePack[idx1] << ",";);
    }
    dbg_executes(errs() << "\n";);
    storePacks.push_back(storePack);
  }

  return storePacks;
}

bool runImpl(Function &F, ScalarEvolution *SE_,
             TargetTransformInfo *TTI_,
             TargetLibraryInfo *TLI_, AliasAnalysis *AA_,
             LoopInfo *LI_, DominatorTree *DT_) //,
                                                //AssumptionCache *AC_, DemandedBits *DB_,
                                                //OptimizationRemarkEmitter *ORE_)
{
  const DataLayout *DL = &F.getParent()->getDataLayout();

  if (!DL)
    return false;
  //don't vectorize if no vector registers
  //if (TTI_->getNumberOfRegisters(true))
  //  return false;

  BoUpSLP R(&F, SE_, DL, TTI_, TLI_, AA_, LI_, DT_);
  for (po_iterator<BasicBlock *> it = po_begin(&F.getEntryBlock()); it != po_end(&F.getEntryBlock()); it++)
  {
    BasicBlock *bb = *it;
    //dbg_executes(errs()<<"WARNING: "<<*bb<<"\n";);
    std::list<std::vector<Value *>> seedPacks = collectStores(bb, R);
    while (!seedPacks.empty())
    {
      std::vector<Value *> seedPack = seedPacks.front();
      seedPacks.pop_front();
      if (seedPack.size() <= 1)
        continue;
      R.deleteTree();
      R.buildTree(seedPack);
      R.vectorizeTree();
    }
  }

  return false;
}

struct MySLPPass : public PassInfoMixin<MySLPPass>
{

  PreservedAnalyses run(Function &F,
                        FunctionAnalysisManager &FAM)
  {

    auto *SE = &FAM.getResult<ScalarEvolutionAnalysis>(F);
    auto *TTI = &FAM.getResult<TargetIRAnalysis>(F);
    auto *TLI = FAM.getCachedResult<TargetLibraryAnalysis>(F);
    auto *AA = &FAM.getResult<AAManager>(F);
    auto *LI = &FAM.getResult<LoopAnalysis>(F);
    auto *DT = &FAM.getResult<DominatorTreeAnalysis>(F);
    // auto *AC = &FAM.getResult<AssumptionAnalysis>(F);
    // auto *DB = &FAM.getResult<DemandedBitsAnalysis>(F);
    // auto *ORE = &FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    bool Changed = runImpl(F, SE, TTI, TLI, AA, LI, DT); //, AC, DB, ORE);
    if (!Changed)
      return PreservedAnalyses::all();

    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    PA.preserve<AAManager>();
    PA.preserve<GlobalsAA>();
    return PA;
  }
};
struct MySLPLegacyPass : public FunctionPass
{
  static char ID; // Pass identification
  MySLPLegacyPass() : FunctionPass(ID)
  {
  }

  // Entry point for the overall scalar-replacement pass
  bool runOnFunction(Function &F)
  {
    if (skipFunction(F))
      return false;
    dbg_executes(errs() << "Warning: run on function\n";);
    auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    //DataLayoutPass *DLP = getAnalysisIfAvailable<DataLayoutPass>();
    //DL = DLP ? &DLP->getDataLayout() : nullptr;
    auto *TLIP = getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
    auto *TLI = TLIP ? &TLIP->getTLI() : nullptr;
    auto *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    dbg_executes(errs() << "Warning: dependency" << SE << " " << TTI << " " << TLI << " " << AA << " " << LI << " " << DT << " "
                        << "\n";);
    return runImpl(F, SE, TTI, TLI, AA, LI, DT); //, AC, DB, ORE);
  }
  // getAnalysisUsage - List passes required by this pass.  We also know it
  // will not alter the CFG, so say so.
  void getAnalysisUsage(AnalysisUsage &AU) const override
  {
    FunctionPass::getAnalysisUsage(AU);
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<AAResultsWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.setPreservesCFG();
  }
};

//} // end anonymous namespace

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/PassSupport.h"
static void registerMyPass(const PassManagerBuilder &,
                           legacy::PassManagerBase &PM)
{
  PM.add(new MySLPLegacyPass());
}
static RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_ModuleOptimizerEarly,
                   registerMyPass);

char MySLPLegacyPass::ID = 0;
static RegisterPass<MySLPLegacyPass> X("slpvect-kdw",
                                       "SLPVectorizer (by <kunwu2> and <daweis2>)",
                                       false /* does not modify the CFG */,
                                       false /* transformation, not just analysis */);

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo()
{
  return {
      LLVM_PLUGIN_API_VERSION, "MySLPPass", "v0.1",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "slpvect-kdw")
              {
                FPM.addPass(MySLPPass());
                return true;
              }
              return false;
            });
      }};
}
