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
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>

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

struct lessThanSmallBitVector
{
  bool operator()(const SmallBitVector &a, const SmallBitVector b) const
  {
    SmallBitVector result(a);
    result |= b;
    return (result == b) && (a.count() < b.count());
  }
};

static void uniquePushBackSmallBitVector(std::vector<SmallBitVector> &cuts, SmallBitVector element)
{
  for (int idx = 0; idx < cuts.size(); idx++)
  {
    if (cuts[idx] == element)
      return;
  }
  cuts.push_back(element);
}

static void dumpSmallBitVector(SmallBitVector &BV)
{
  errs() << "{";
  for (unsigned VI : BV.set_bits())
  {
    errs() << VI;
    if (BV.find_next(VI) >= 0)
      errs() << ' ';
  }
  errs() << "}";
}

void BoUpSLP::printVectorizableTree()
{
  dbg_executes(errs() << "printing vectorizableTree\n";);
  for (unsigned int idx = 0; idx < VectorizableTree.size(); idx++)
  {
    dbg_executes(errs() << "idx(" << idx << "," << VectorizableTree[idx].idx << ") " << *VectorizableTree[idx].Scalars[0] << "\n";);
    dbg_executes(errs() << "idx(" << idx << ") children: ";);
    for (unsigned int childIdx = 0; childIdx < EntryChildrenID[idx].size(); childIdx++)
    {
      dbg_executes(errs() << "(" /*<< EntryChildrenEntries[idx][childIdx]->idx << ","*/ << EntryChildrenID[idx][childIdx] << ") , ";);
    }
    dbg_executes(errs() << "\n";);
  }
}

//we only need to deschedule nodes in cut and nodes whose NeedToGather is set because nodes in the NeedToGather won't be scheduled.
void BoUpSLP::descheduleExternalNodes(SmallBitVector cut, SmallBitVector nodesNeedToUnsetNeedToGather)
{
  for (int idx = 0; idx < VectorizableTree.size(); idx++)
  {
    if ((!cut[idx]) && (!VectorizableTree[idx].NeedToGather))
    {
      BasicBlock *BB = getSameBlock(VectorizableTree[idx].Scalars);
      // if (!BlocksSchedules.count(BB))
      // {
      //   BlocksSchedules[BB] = llvm::make_unique<BlockScheduling>(BB);
      // }
      assert(BlocksSchedules.count(BB) && "unexpected in descheduleExternalNodes()");
      auto &BSRef = BlocksSchedules[BB];
      auto &BS = *BSRef.get();
      BS.cancelScheduling(VectorizableTree[idx].Scalars);
    }
  }
}

void BoUpSLP::buildTree_rec(ArrayRef<Value *> VL, unsigned Depth, int parentNodeIdx)
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
    newTreeEntry(VL, false, parentNodeIdx);
    return;
  }

  //if not the same type or invalid vector element type, stop
  if (!getSameType(VL))
  {
    dbg_executes(errs() << "MySLP: buildTree not same type\n";);
    newTreeEntry(VL, false, parentNodeIdx);
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
        newTreeEntry(VL, false, parentNodeIdx);
        return;
      }
    }
    else
    {
      if (!isValidElementType(VL[idx_vl]->getType()))
      {
        dbg_executes(errs() << "MySLP: buildTree not valid type\n";);
        newTreeEntry(VL, false, parentNodeIdx);
        return;
      }
    }
  }

  //if all Constant or isSplat there is cheap way of gathering them
  if (allConstant(VL) || isSplat(VL))
  {
    newTreeEntry(VL, false, parentNodeIdx);
    return;
  }

  //if the instructions are not in the same block or same OP stop
  unsigned OpCode = getSameOpcode(VL);
  BasicBlock *BB = getSameBlock(VL);
  if (!OpCode || !BB)
  {
    dbg_executes(errs() << "MySLP: buildTree not same block or same opcode\n";);
    newTreeEntry(VL, false, parentNodeIdx);
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
        newTreeEntry(VL, false, parentNodeIdx); //partial overlap
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
    newTreeEntry(VL, false, parentNodeIdx);
    return;
  }

  // check no duplication in this bundle
  std::set<Value *> uniqueVLElement;
  for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
  {
    if (uniqueVLElement.count(VL[idx_vl]))
    {
      dbg_executes(errs() << "MySLP: buildTree duplicate item in bundle\n";);
      newTreeEntry(VL, false, parentNodeIdx);
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
    newTreeEntry(VL, false, parentNodeIdx); //cannot schedule
    return;
  }

  //cast the first instruction for use
  Instruction *VL0 = dyn_cast<Instruction>(VL[0]);
  if (!VL0)
  {
    dbg_executes(errs() << "shouldn't be here Instruction cast failed\n";);
    BS.cancelScheduling(VL);
    newTreeEntry(VL, false, parentNodeIdx);
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
        newTreeEntry(VL, false, parentNodeIdx);
        return;
      }
      if (idx_vl != VL.size() - 1)
      {
        if (!isConsecutiveAccess(VL[idx_vl], VL[idx_vl + 1]))
        {
          dbg_executes(errs() << "MySLP: not vectorized due to LoadInst does not satisfy isConsecutiveAccess\n";);
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false, parentNodeIdx);
          return;
        }
      }
    }
    newTreeEntry(VL, true, parentNodeIdx);
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
        newTreeEntry(VL, false, parentNodeIdx);
        return;
      }
      if (idx_vl != VL.size() - 1)
      {
        if (!isConsecutiveAccess(VL[idx_vl], VL[idx_vl + 1]))
        {
          dbg_executes(errs() << "MySLP: not vectorized due to StoreInst does not satisfy isConsecutiveAccess\n";);
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false, parentNodeIdx);
          return;
        }
      }
    }
    TreeEntry *thisNode = newTreeEntry(VL, true, parentNodeIdx);
    int thisNodeIdx = thisNode->idx;
    // for (int idx_operand = 0; idx_operand < dyn_cast<StoreInst>(VL[0])->getNumOperands(); idx_operand++)
    // {
    std::vector<Value *> operands;
    for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
    {
      operands.push_back(dyn_cast<StoreInst>(VL[idx_vl])->getOperand(0));
    }
    buildTree_rec(operands, Depth + 1, thisNodeIdx);
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
        newTreeEntry(VL, false, parentNodeIdx);
        return;
      }
    }
    TreeEntry *thisNode = newTreeEntry(VL, true, parentNodeIdx);
    int thisNodeIdx = thisNode->idx;
    for (int idx_operand = 0; idx_operand < VL0->getNumOperands(); idx_operand++)
    {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<CmpInst>(VL[idx_vl])->getOperand(idx_operand));
      }
      buildTree_rec(operands, Depth + 1, thisNodeIdx);
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
    TreeEntry *thisNode = newTreeEntry(VL, true, parentNodeIdx);
    int thisNodeIdx = thisNode->idx;
    for (int idx_operand = 0; idx_operand < VL0->getNumOperands(); idx_operand++)
    {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<Instruction>(VL[idx_vl])->getOperand(idx_operand));
      }
      buildTree_rec(operands, Depth + 1, thisNodeIdx);
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
      newTreeEntry(VL, false, parentNodeIdx);
      return;
    }
    for (int idx = 1; idx < VL.size(); idx++)
    {
      Type *curr_type = dyn_cast<Instruction>(VL[idx])->getType();
      if ((curr_type != SrcTy) || (!isValidElementType(curr_type)))
      { //not the same type or invalid vector type
        BS.cancelScheduling(VL);
        newTreeEntry(VL, false, parentNodeIdx);
        return;
      }
    }
    TreeEntry *thisNode = newTreeEntry(VL, true, parentNodeIdx);
    int thisNodeIdx = thisNode->idx;
    for (int idx_operand = 0; idx_operand < VL0->getNumOperands(); idx_operand++)
    {
      std::vector<Value *> operands;
      for (int idx_vl = 0; idx_vl < VL.size(); idx_vl++)
      {
        operands.push_back(dyn_cast<Instruction>(VL[idx_vl])->getOperand(idx_operand));
      }
      buildTree_rec(operands, Depth + 1, thisNodeIdx);
    }
    return;
  }
//do not deal with
#include "buildTree_rec.casedef"
  default:
  { //TODO: what about alloca, atomic operations that bump into this scheme?
    llvm_unreachable("unexpected Opcode");
  }
  }
}

//set NeedToGather for each of the children nodes of the cut
// return the nodes indices whose NeedToGather flag is modified
SmallBitVector BoUpSLP::setNeedToGather(SmallBitVector cut)
{
  //for each node in cut
  //for each child
  //if not NeedToGather
  //set NeedToGather
  //set its index in the result SmallBitVector
  dbg_executes(errs() << "entering setNeedToGather() \n";);
  SmallBitVector modifiedIndex(VectorizableTree.size());
  if(cut.count()==0){//only one node in the graph: root NeedToGather==true
    modifiedIndex.set(0);
    VectorizableTree[0].NeedToGather=true;
  }
  else{
    for (auto idx : cut.set_bits())
    {
      if (!VectorizableTree[idx].NeedToGather)
      {
        dbg_executes(errs() << "(" << idx << ") children: ";);
        for (unsigned int childIdx = 0; childIdx < EntryChildrenID[idx].size(); childIdx++)
        {
          if (!VectorizableTree[EntryChildrenID[idx][childIdx]].NeedToGather && !cut[EntryChildrenID[idx][childIdx]])
          { //need to gather
            dbg_executes(errs() << EntryChildrenID[idx][childIdx] << " , ";);
            modifiedIndex.set(EntryChildrenID[idx][childIdx]);
            //assert(EntryChildrenEntries[idx][childIdx]->idx==VectorizableTree[EntryChildrenEntries[idx][childIdx]->idx].idx&&"idx mismatch in EntryChildrenEntries[idx]");
            VectorizableTree[EntryChildrenID[idx][childIdx]].NeedToGather = true;
            //assert(EntryChildrenEntries[idx][childIdx]->idx==EntryChildrenID[idx][childIdx]);
          }
        }
      }
    }
  }

  dbg_executes(errs() << "\nsetNeedToGather() ";);
  dumpSmallBitVector(modifiedIndex);
  dbg_executes(errs() << "\n";);
  return modifiedIndex;
}

//unset NeedToGather for each of the node indicated by the argument
//the argument is usually the output of the BoUpSLP::setNeedToGather(SmallBitVector cut)
void BoUpSLP::unsetNeedToGather(SmallBitVector nodesNeedToUnset)
{
  dbg_executes(errs() << "unsetNeedToGather() ";);
  dumpSmallBitVector(nodesNeedToUnset);
  dbg_executes(errs() << "\n";);

  for (auto idx : nodesNeedToUnset.set_bits())
  {
    assert(VectorizableTree[idx].NeedToGather && "nodesNeedToUnset contains nodes whose NeedToGather!=true.");
    VectorizableTree[idx].NeedToGather = false;
  }
}

void BoUpSLP::calcExternalUses()
{
  assert(ExternalUses.empty() && "ExternalUses non empty before calling calcExternalUses");
  for (unsigned int idx_te = 0; idx_te < VectorizableTree.size(); idx_te++)
  {
    TreeEntry *TE = &VectorizableTree[idx_te];
    if (TE->NeedToGather)
      continue; //don't need to extractelement for non-vectorizable leaves use as they are not vectorized.
    for (unsigned int idx2_scalar = 0; idx2_scalar < TE->Scalars.size(); idx2_scalar++)
    {
      for (User *U : TE->Scalars[idx2_scalar]->users())
      {
        if (!ScalarToTreeEntry[TE->Scalars[idx2_scalar]])
          ExternalUses.push_back(ExternalUser(TE->Scalars[idx2_scalar], U, idx2_scalar));
      }
    }
  }
}

void BoUpSLP::calcExternalUses(SmallBitVector cut)
{
  assert(ExternalUses.empty() && "ExternalUses non empty before calling calcExternalUses");
  dbg_executes(errs()<<"calcExternalUses(): ";);
  dumpSmallBitVector(cut);
  dbg_executes(errs()<<"\n";);
  for (auto idx_te : cut.set_bits())
  {
    TreeEntry *TE = &VectorizableTree[idx_te];
    if (TE->NeedToGather){
      llvm_unreachable("calcExternalUses bumps into NeedToGather leaves according to cut");
      continue; //don't need to extractelement for non-vectorizable leaves use as they are not vectorized.
      }
    for (unsigned int idx2_scalar = 0; idx2_scalar < TE->Scalars.size(); idx2_scalar++)
    {
      for (User *U : TE->Scalars[idx2_scalar]->users())
      {
        if (!ScalarToTreeEntry[TE->Scalars[idx2_scalar]] || !cut[ScalarToTreeEntry[TE->Scalars[idx2_scalar]]])
          ExternalUses.push_back(ExternalUser(TE->Scalars[idx2_scalar], U, idx2_scalar));
      }
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
  buildTree_rec(Roots, 0, -1);
  calcExternalUses();
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
  for (unsigned int i = 0; i < VL.size(); i++)
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
      for (unsigned int pos = 0; pos < E->Scalars.size(); pos++)
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
  for (unsigned int idx = 0; idx < VL.size(); idx++)
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
  IRBuilder<>::InsertPointGuard Guard(Builder);
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
    if (isa<BinaryOperator>(scalar) && scalar->isCommutative())
    {
      reorderInputsAccordingToOpcode(E->Scalars, LHS_scalars, RHS_scalars);
    }
    else
    {
      for (unsigned int i = 0; i < E->Scalars.size(); i++)
      {
        Instruction *I = cast<Instruction>(E->Scalars[i]);
        LHS_scalars.push_back(I->getOperand(0));
        RHS_scalars.push_back(I->getOperand(1));
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
    for (unsigned int i = 0; i < E->Scalars.size(); i++)
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
    // Single operand
    // Step 1, collect operands
    ValueList operand_scalars;
    for (unsigned int i = 0; i < E->Scalars.size(); i++)
    {
      auto *I = cast<StoreInst>(E->Scalars[i]);
      operand_scalars.push_back(I->getOperand(0));
    }

    // Step 2, vectorize operands
    setInsertPointAfterBundle(E->Scalars);
    Value *operand_vector = vectorizeTree_rec(operand_scalars);

    if (alreadyVectorized(E->Scalars))
    {
      // it is possible that when vectorizing the operands, we also vectorized this entry.
      return alreadyVectorized(E->Scalars);
    }

    // Step 3, create a new Instruction with the vectorized operands.
    CastInst *CI = dyn_cast<CastInst>(scalar);
    Value *V = Builder.CreateCast(CI->getOpcode(), operand_vector, vector_type);
    E->VectorizedValue = V;
    return V;
  }
  case Instruction::FCmp:
  case Instruction::ICmp:
  {
    // For binary operators. Operands: LHS and RHS
    // Step 1, collect operands
    ValueList LHS_scalars, RHS_scalars;
    for (unsigned int i = 0; i < E->Scalars.size(); i++)
    {
      Instruction *I = cast<Instruction>(E->Scalars[i]);
      LHS_scalars.push_back(I->getOperand(0));
      RHS_scalars.push_back(I->getOperand(1));
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
    CmpInst::Predicate P0 = dyn_cast<CmpInst>(scalar)->getPredicate();
    Value *V;
    if (Opcode == Instruction::FCmp)
      V = Builder.CreateFCmp(P0, LHS_vector, RHS_vector);
    else
      V = Builder.CreateICmp(P0, LHS_vector, RHS_vector);

    E->VectorizedValue = V;
    return V;
  }
  case Instruction::Select:
  {
    // Triple operands: True, False, Condition
    // Step 1, collect operands
    ValueList true_scalars, false_scalars, condition_scalars;
    for (unsigned int i = 0; i < E->Scalars.size(); i++)
    {
      Instruction *I = cast<Instruction>(E->Scalars[i]);
      condition_scalars.push_back(I->getOperand(0));
      true_scalars.push_back(I->getOperand(1));
      false_scalars.push_back(I->getOperand(2));
    }

    // Step 2, vecotrize operands
    setInsertPointAfterBundle(E->Scalars);
    auto true_vector = vectorizeTree_rec(true_scalars);
    auto false_vector = vectorizeTree_rec(false_scalars);
    auto condition_vector = vectorizeTree_rec(condition_scalars);

    if (alreadyVectorized(E->Scalars))
    {
      // it is possible that when vectorizing the operands, we also vectorized this entry.
      return alreadyVectorized(E->Scalars);
    }

    // Step 3, create a new Instruction with the vectorized operands.
    Value *V = Builder.CreateSelect(condition_vector, true_vector, false_vector);
    E->VectorizedValue = V;
    return V;
  }
//the following are either not implemented or copied from the source code
#include "vectorizeTree_rec.casedef"
  default:
    llvm_unreachable("unknown inst");
  }
  return nullptr;
}

// Vectorize the tree from root recursively.
Value *BoUpSLP::vectorizeTree()
{
  // Schedule all blocks
  for (auto &BSIter : BlocksSchedules)
  {
    scheduleBlock(BSIter.second.get());
  }

  // Step 1, recursively vectorize starting from the root.
  Builder.SetInsertPoint(&F->getEntryBlock().front());
  do_vectorizeTree_rec(&VectorizableTree[0]);

  // Step 2, we need to ExtractElement from the designated lane of the
  // vectorized value for uses external to the tree, since those scalars are
  // used in some new InsertElement instructions.
  for (UserList::iterator it = ExternalUses.begin(), e = ExternalUses.end();
       it != e; ++it)
  {
    Value *scalar = it->Scalar;
    llvm::User *U = it->User;

    // Skip users that we already handled.
    if (std::find(scalar->user_begin(), scalar->user_end(), U) ==
        scalar->user_end())
      continue;

    // found the scalar in the tree and the corresponding vector and the position
    TreeEntry *E = &VectorizableTree[ScalarToTreeEntry[scalar]];
    Value *vector = E->VectorizedValue;
    Value *pos = Builder.getInt32(it->Lane);

    // insert an ExtractElement instruction for this pair of scalar and use
    if (isa<Instruction>(vector))
    {
      if (PHINode *PH = dyn_cast<PHINode>(U))
      {
        // used in a PHI node
        // found the incoming block corresponding to this scalar and insert after this block
        for (unsigned int i = 0; i < PH->getNumIncomingValues(); i++)
        {
          if (scalar == PH->getIncomingValue(i))
          {
            Builder.SetInsertPoint(PH->getIncomingBlock(i)->getTerminator());
            // create an ExtractElement instruction
            Value *I = Builder.CreateExtractElement(vector, pos);
            // replace use
            PH->setOperand(i, I);
          }
        }
      }
      else
      {
        Builder.SetInsertPoint(cast<Instruction>(U));
        // create an ExtractElement instruction
        Value *I = Builder.CreateExtractElement(vector, pos);
        // replace use
        U->replaceUsesOfWith(scalar, I);
      }
    }
    else
    {
      Builder.SetInsertPoint(&F->getEntryBlock().front());
      // create an ExtractElement instruction
      Value *I = Builder.CreateExtractElement(vector, pos);
      // replace use
      U->replaceUsesOfWith(scalar, I);
    }
  }

  // Step 3, replace all the uses of scalars with undef such that these uses will be removed
  for (unsigned int i = 0; i < VectorizableTree.size(); i++)
  {
    TreeEntry *E = &VectorizableTree[i];
    for (unsigned int j = 0; j < E->Scalars.size(); j++)
    {
      Value *scalar = E->Scalars[j];
      // Since the users of gathered values have already been replaced with
      // ExtractElement instructions, we should skip those cases.
      if (E->NeedToGather)
        continue;
      // otherwise, replace all uses of this scalar with undef
      // such that this scalar will be standaloned from the IR and removed by DCE
      if (!scalar->getType()->isVoidTy())
      {
        scalar->replaceAllUsesWith(UndefValue::get(scalar->getType()));
      }
      cast<Instruction>(scalar)->eraseFromParent();
    }
  }

  Builder.ClearInsertionPoint();

  return VectorizableTree[0].VectorizedValue;
}

Value *BoUpSLP::vectorizeTree(SmallBitVector cut)
{
  SmallBitVector nodesNeedToUnset = setNeedToGather(cut);

  descheduleExternalNodes(cut, nodesNeedToUnset);
  Value *result = vectorizeTree();
  unsetNeedToGather(nodesNeedToUnset);
  return result;
}



//get the cost of subtree indicated by cut
//cut
int BoUpSLP::getTreeCost(SmallBitVector cut){
  dbg_executes(errs() << "getTreeCost(cut) cut ";);
  dumpSmallBitVector(cut);
  dbg_executes(errs() << "\n";);
  SmallBitVector nodesNeedToUnset = setNeedToGather(cut);

  SmallBitVector allNodesInCut = cut | nodesNeedToUnset;
  std::vector<unsigned int> allNodesInCutVec;
  

  for (auto idx : allNodesInCut.set_bits())
  {
    allNodesInCutVec.push_back(idx);
  }

  int result = do_getTreeCost( allNodesInCutVec);

  unsetNeedToGather(nodesNeedToUnset);
  return result;
}


int BoUpSLP::do_getTreeCost(std::vector<unsigned int>& allNodesInCutVec)
{
  int Cost = 0;
  LLVM_DEBUG(dbgs() << "SLP: Calculating cost for tree of size " << allNodesInCutVec.size() << ".\n");

  // We only vectorize tiny trees if it is fully vectorizable.
  if (allNodesInCutVec.size() < 3 && !isFullyVectorizableTinyTree(allNodesInCutVec))
  {
    if (!allNodesInCutVec.size())
    {
      assert(!ExternalUses.size() && "We should not have any external users");
    }
    return INT_MAX;
  }

  unsigned BundleWidth = VectorizableTree[0].Scalars.size();

  for (unsigned i = 0, e = allNodesInCutVec.size(); i != e; ++i)
  {
    int C = getEntryCost(&VectorizableTree[allNodesInCutVec[i]]);
    LLVM_DEBUG(dbgs() << "SLP: Adding cost " << C << " for bundle that starts with "
                      << *VectorizableTree[allNodesInCutVec[i]].Scalars[0] << " .\n");
    Cost += C;
  }

  SmallSet<Value *, 16> ExtractCostCalculated;
  int ExtractCost = 0;
  for (UserList::iterator I = ExternalUses.begin(), E = ExternalUses.end();
       I != E; ++I)
  {
    // We only add extract cost once for the same scalar.
    if (!ExtractCostCalculated.insert(I->Scalar).second)
      continue;

    VectorType *VecTy = VectorType::get(I->Scalar->getType(), BundleWidth);
    ExtractCost += TTI->getVectorInstrCost(Instruction::ExtractElement, VecTy,
                                           I->Lane);
  }

  LLVM_DEBUG(dbgs() << "SLP: Total Cost " << Cost + ExtractCost << ".\n");

  return Cost + ExtractCost;
}

void BoUpSLP::levelOrderTraverse(std::vector<unsigned int> &levels, std::vector<TreeEntry *> &entries)
{
  levels.clear();
  entries.clear();
  std::list<TreeEntry *> workList;
  std::list<unsigned int> workListLevel;

  workList.push_back(&VectorizableTree[0]);
  workListLevel.push_back(0);
  while (!workList.empty())
  {
    TreeEntry *currEntry = workList.front();
    unsigned int currLevel = workListLevel.front();
    workList.pop_front();
    workListLevel.pop_front();
    levels.push_back(currLevel);
    entries.push_back(currEntry);
    if (!currEntry->NeedToGather)
    { //handle the case where root node NeedToGather
      for (unsigned childIdx = 0; childIdx < EntryChildrenID[currEntry->idx].size(); childIdx++)
      {
        if (!VectorizableTree[EntryChildrenID[currEntry->idx][childIdx]].NeedToGather)
        { //is not NeedToGather leave
          workList.push_back(&VectorizableTree[EntryChildrenID[currEntry->idx][childIdx]]);
          workListLevel.push_back(currLevel + 1);
        }
      }
    }
  }
}

template <typename T>
std::vector<T> unique_index(std::vector<T> sortedArray)
{
  if (sortedArray.size() == 0)
    return std::vector<T>();
  std::vector<T> result;
  result.push_back(0);
  T lastUnique = sortedArray[0];
  for (int idx = 1; idx < sortedArray.size(); idx++)
  {
    if (sortedArray[idx] != lastUnique)
    {
      lastUnique = sortedArray[idx];
      result.push_back(idx);
    }
  }
  result.push_back(sortedArray.size());
  return result;
}

static SmallBitVector enlistAllLevelNodeCutInLevel(SmallBitVector lastLevelAllNodeCut, std::vector<SmallBitVector> &cuts, unsigned int levelStartPos, unsigned int levelEndPos, std::vector<BoUpSLP::TreeEntry *> &entriesInLevelOrder)
{
  SmallBitVector result(lastLevelAllNodeCut);
  for (unsigned int idx = levelStartPos; idx < levelEndPos; idx++)
  {
    assert(((idx == 0) || (!entriesInLevelOrder[idx]->NeedToGather)) && "NeedToGather leaves shouldn't be incldued in level order");
    if (!entriesInLevelOrder[idx]->NeedToGather)
      result.set(entriesInLevelOrder[idx]->idx);
  }
  if (levelEndPos - levelStartPos != 1)
  {
    assert(result != lastLevelAllNodeCut && "SmallBitVector doesn't use copy constructor");
  }
  uniquePushBackSmallBitVector(cuts, result);
  return result;
}

static void enlistNextLevelEachNeighbourCut(SmallBitVector lastLevelAllNodeCut, std::vector<SmallBitVector> &cuts, unsigned int nextLevelStartPos, unsigned int nextLevelEndPos, std::vector<BoUpSLP::TreeEntry *> &entriesInLevelOrder, unsigned int maxNumResults)
{

  for (unsigned int idx = nextLevelStartPos; idx < std::min(nextLevelEndPos, nextLevelStartPos + maxNumResults); idx++)
  {
    SmallBitVector currResult(lastLevelAllNodeCut);
    assert(!entriesInLevelOrder[idx]->NeedToGather && "NeedToGather leaves shouldn't be incldued in level order");
    currResult.set(entriesInLevelOrder[idx]->idx);
    assert(currResult != lastLevelAllNodeCut && "SmallBitVector doesn't use copy constructor || level order traverse repetitively enlist some entry");
    uniquePushBackSmallBitVector(cuts, currResult);
  }
}

static void printEntries(ArrayRef<BoUpSLP::TreeEntry *> entries)
{
  for (unsigned int idx = 0; idx < entries.size(); idx++)
  {
    dbg_executes(errs() << "entries (" << entries[idx]->NeedToGather << "): " << *entries[idx]->Scalars[0] << "\n";);
  }
}

//won't return cut with no nodes
//the cut SmallBitVector doesn't involve NeedToGather leaves, i.e., those bits corresponding to NeedToGather leaves won't be set even if they are in this cut.
std::vector<SmallBitVector> BoUpSLP::getCuts(unsigned int allNeighbourThreshold)
{

  std::vector<SmallBitVector> levelAllNodesCuts;
  std::list<TreeEntry *> workList;
  std::list<SmallBitVector> workListAlreadyInCut;
  //SmallBitVector currVisited(VectorizableTree.size());//indexing the nodes according to VectorizableTree means NeedToGather leaves also takes up bits but we don't count them in cut SmallBitVector.
  workList.push_back(&VectorizableTree[0]);
  //currVisited.set(0);
  workListAlreadyInCut.push_back(SmallBitVector(VectorizableTree.size()));
  workListAlreadyInCut.front().set(0);

  std::vector<unsigned int> levels;
  std::vector<TreeEntry *> entriesInLevelOrder;
  levelOrderTraverse(levels, entriesInLevelOrder);
  std::vector<unsigned int> unique_level_idx = unique_index<unsigned int>(levels);
  //printEntries(entriesInLevelOrder);

  //enlist subgraphs
  //BUGFIXED: need to deduplicate here
  SmallBitVector lastLevelAllNodeCut(VectorizableTree.size());
  for (unsigned int currLevel = 0; currLevel <= levels[levels.size() - 1]; currLevel++)
  {
    lastLevelAllNodeCut = enlistAllLevelNodeCutInLevel(lastLevelAllNodeCut, levelAllNodesCuts, unique_level_idx[currLevel], unique_level_idx[currLevel + 1], entriesInLevelOrder);
  }
  std::vector<SmallBitVector> results(levelAllNodesCuts.begin(), levelAllNodesCuts.end());
  for (unsigned int currLevel = 0; currLevel < levels[levels.size() - 1]; currLevel++)
  {
    if ((results.size() - levels[levels.size() - 1] - 1) < allNeighbourThreshold)
    {
      enlistNextLevelEachNeighbourCut(levelAllNodesCuts[currLevel], results, unique_level_idx[currLevel + 1], unique_level_idx[currLevel + 2], entriesInLevelOrder, allNeighbourThreshold - (results.size() - levels[levels.size() - 1] - 1));
    }
  }

  // while(!workList.empty()){
  //   TreeEntry* curr=workList.front();
  //   workList.pop_front();
  //   SmallBitVector currVisited = workListAlreadyInCut.front();
  //   enlistAllNeighbourCut(currVisited,result);
  //   enlistEachNeighbourCut(currVisited,result,allNeighbourThreshold);
  //   workListAlreadyInCut.pop_front();
  //   for (unsigned childIdx = 0;childIdx=EntryChildrenID[curr->idx].size();childIdx++){
  //     if (EntryChildrenID[curr->idx][childIdx]!=-1){
  //       if (result.size()>)
  //       workList.push_back(&VectorizableTree[EntryChildrenID[curr->idx][childIdx]]);
  //       workListAlreadyInCut.push_back(currVisited);
  //       workListAlreadyInCut.back().set(EntryChildrenID[curr->idx][childIdx]);
  //     }
  //   }
  // }

  return results;
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

#ifdef NAIVE_IMPL
      R.deleteTree();
      R.buildTree(seedPack);
      R.clearExternalUses();
      R.calcExternalUses();
      R.vectorizeTree();
#endif

#ifndef TSLP_IMPL
      dbg_executes(errs() << "seedPack[0] " << *seedPack[0] << "\n";);
      dbg_executes(errs() << "try to get type: " << *(dyn_cast<StoreInst>(seedPack[0])->getOperand(0)) << "\n";);
      unsigned int MinNumElementInVector = TTI_->getMinVectorRegisterBitWidth() / DL->getTypeSizeInBits(dyn_cast<StoreInst>(seedPack[0])->getOperand(0)->getType());
      unsigned int MaxNumElementInVector = TTI_->getRegisterBitWidth(true);
      std::vector<SmallBitVector> BestCutInEachChunk;
      unsigned int BestNumElementInVector = -1; //if no vectorization is profitable, the final vectorize() step will be automatically skipped
      int BestCost=0;

      //find the best (numElementInVectorRegister, cuts in each chunk)
      for (unsigned int numElementInVector = MinNumElementInVector; numElementInVector <= MaxNumElementInVector; numElementInVector *= 2)
      {
        std::vector<int> CurrNumEleBestCostInEachChunk;
        std::vector<SmallBitVector> CurrNumEleBestCutInEachChunk;
        dbg_executes(errs() << "numElement(" << numElementInVector << ") best cut: ";);
        for (unsigned int chunkIdx = 0; chunkIdx < seedPack.size() / numElementInVector; chunkIdx++)//todo use another for loop to investigate different offset
        {
          std::vector<Value *> chunkSeed(seedPack.cbegin() + chunkIdx * (numElementInVector), seedPack.cbegin() + (chunkIdx + 1) * (numElementInVector));
          dbg_executes(errs() << "chunk instr: ";);
          for (unsigned int instrIdx = 0; instrIdx < chunkSeed.size(); instrIdx++)
          {
            dbg_executes(errs() << *chunkSeed[instrIdx] << ", ";);
          }
          dbg_executes(errs() << "\n";);
          R.deleteTree();
          R.buildTree(chunkSeed);
          R.printVectorizableTree();
          std::vector<SmallBitVector> Cuts = R.getCuts(AllNeighbourThreshold);

          //search for the best cut
          int CurrChunkBestCost = 0;
          SmallBitVector CurrChunkBestCut(R.getVectorizableTreeSize()); //the default option is not to vectorize
          for (unsigned int cutIdx = 0; cutIdx < Cuts.size(); cutIdx++)
          {
            R.clearExternalUses();
            R.calcExternalUses(Cuts[cutIdx]);
            int ThisCost = R.getTreeCost(Cuts[cutIdx]);
            if (ThisCost < CurrChunkBestCost)
            {
              CurrChunkBestCost = ThisCost;
              CurrChunkBestCut = Cuts[cutIdx];
            }
          }
          CurrNumEleBestCostInEachChunk.push_back(CurrChunkBestCost);
          CurrNumEleBestCutInEachChunk.push_back(CurrChunkBestCut);
          dbg_executes(dumpSmallBitVector(CurrChunkBestCut););
          dbg_executes(errs() << "(" << CurrChunkBestCost << "), ";);
        }
        dbg_executes(errs() << "\n";);

        int CurrNumEleBestCost = std::accumulate(CurrNumEleBestCostInEachChunk.begin(), CurrNumEleBestCostInEachChunk.end(), 0);
        if (CurrNumEleBestCost < BestCost)
        {
          BestCost = CurrNumEleBestCost;
          BestCutInEachChunk = CurrNumEleBestCutInEachChunk;
          BestNumElementInVector = numElementInVector;
        }
      }
      dbg_executes(errs() << "Global best cut numElement(" << BestNumElementInVector << ") Cost" << BestCost << ": ";);
      for (unsigned int cutIdx = 0; cutIdx < BestCutInEachChunk.size(); cutIdx++)//TODO: subtree change (Gather() or undefifying corrupts) due to partial vectorization. only use the best numElement
      {
        dbg_executes(dumpSmallBitVector(BestCutInEachChunk[cutIdx]););
        dbg_executes(errs() << ", ";);
      }
      dbg_executes(errs() << "\n";);
      //apply the best (numElementInVectorRegister, cuts in each chunk)
      for (unsigned int chunkIdx = 0; chunkIdx < seedPack.size() / BestNumElementInVector; chunkIdx++)
      {
        R.deleteTree();
        std::vector<Value *> chunkSeed(seedPack.cbegin() + chunkIdx * (BestNumElementInVector), seedPack.cbegin() + (chunkIdx + 1) * (BestNumElementInVector));
        R.buildTree(chunkSeed);
        R.clearExternalUses();
        R.calcExternalUses(BestCutInEachChunk[chunkIdx]);
        //R.vectorizeTree();
        R.vectorizeTree(BestCutInEachChunk[chunkIdx]);
      }
#endif
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
