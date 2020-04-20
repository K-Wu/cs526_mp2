#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
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
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/InstrTypes.h"
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

#include "helper_static.cpp"
namespace
{
#include "helper.cpp"

void BoUpSLP::buildTree_rec(ArrayRef<Value *> VL, unsigned Depth)
{
  //if recursion reaches max depth, stop
  if (Depth == RecursionMaxDepth)
  {
    newTreeEntry(VL, false);
    return;
  }

  //if not the same type or invalid vector element type, stop
  if (!getSameType(VL)){
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
        newTreeEntry(VL, false);
        return;
      }
    }
    else
    {
      if (!isValidElementType(VL[idx_vl]->getType()))
      {
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
    newTreeEntry(VL, false);
    return;
  }



  // Check if this is a duplicate of another scalar in the tree.
  if (ScalarToTreeEntry.count(VL[0])){
    TreeEntry* TE = &VectorizableTree[ScalarToTreeEntry[VL[0]]];
    for (int idx_vl = 0; idx_vl < VL.size();idx_vl++){
      if (TE->Scalars[idx_vl]!=VL[idx_vl]){
        newTreeEntry(VL, false);//partial overlap
        return;
      }
    }
    //equal. Don't create new TreeEntry as it overwrites the ScalarToTreeEntry entries.
    //TODO: vectorizeTree() recursion part needs to pass the ArrayRef<Value*> VL rather than the tree node and check whether the ArrayRef<Value*> is in the tree.
    return;
  }

  // check block reachability
  if (!DT->isReachableFromEntry(BB)){
    newTreeEntry(VL,false);
    return;
  }

  // check no duplication in this bundle
  std::set<Value*> uniqueVLElement;
  for(int idx_vl=0;idx_vl<VL.size();idx_vl++){
    if (uniqueVLElement.count(VL[idx_vl])){
      newTreeEntry(VL, false);
      return;
    }
    uniqueVLElement.insert(VL[idx_vl]);
  }

  // check scheduling
  if (!BlocksSchedules.count(BB)){
    BlocksSchedules[BB]=llvm::make_unique<BlockScheduling>(BB);
  }
 auto& BSRef=BlocksSchedules[BB];
 auto& BS = *BSRef.get();
 dbg_executes(errs()<<"WARNING: BS "<<BS.BB<<"\n";);
  if(!BS.tryScheduleBundle(VL,AA)){
    BS.cancelScheduling(VL);
    newTreeEntry(VL,false);//cannot schedule
    return;
  }


  //cast the first instruction for use
  Instruction* VL0=dyn_cast<Instruction>(VL[0]);
  if(!VL0){
    dbg_executes(errs()<<"shouldn't be here Instruction cast failed\n";);
    BS.cancelScheduling(VL);
    newTreeEntry(VL, false);
    return;
  }

  switch(OpCode){
    case Instruction::Load:{
      //ensure simple and consecutive
      for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
        if (!dyn_cast<LoadInst>(VL[idx_vl])->isSimple()){
          dbg_executes(errs()<<"MySLP: not vectorized due to LoadInst is not simple\n";);
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false);
          return;
        }
        if (idx_vl!=VL.size()-1){
          if (!isConsecutiveAccess(VL[idx_vl],VL[idx_vl])){
            dbg_executes(errs()<<"MySLP: not vectorized due to LoadInst does not satisfy isConsecutiveAccess\n";);
            BS.cancelScheduling(VL);
            newTreeEntry(VL, false);
            return;
          }
        }
      }
      newTreeEntry(VL,true);
      for (int idx_operand=0;idx_operand<dyn_cast<LoadInst>(VL[0])->getNumOperands();idx_operand++){
        std::vector<Value*> operands;
        for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
          operands.push_back(dyn_cast<LoadInst>(VL[idx_vl])->getOperand(idx_operand));
        }
        buildTree_rec(operands,Depth+1);
      }
    }
    case Instruction::Store:{
      //ensure simple and consecutive
      for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
        if (!dyn_cast<StoreInst>(VL[idx_vl])->isSimple()){
          dbg_executes(errs()<<"MySLP: not vectorized due to StoreInst is not simple\n";);
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false);
          return;
        }
        if (idx_vl!=VL.size()-1){
          if (!isConsecutiveAccess(VL[idx_vl],VL[idx_vl])){
            dbg_executes(errs()<<"MySLP: not vectorized due to StoreInst does not satisfy isConsecutiveAccess\n";);
            BS.cancelScheduling(VL);
            newTreeEntry(VL, false);
            return;
          }
        }
      }
      newTreeEntry(VL,true);
      for (int idx_operand=0;idx_operand<dyn_cast<StoreInst>(VL[0])->getNumOperands();idx_operand++){
        std::vector<Value*> operands;
        for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
          operands.push_back(dyn_cast<StoreInst>(VL[idx_vl])->getOperand(idx_operand));
        }
        buildTree_rec(operands,Depth+1);
      }

    }
    case Instruction::ICmp:
    case Instruction::FCmp:{
      //ensure the predicate is the same type
      auto VL0Pred = dyn_cast<CmpInst>(VL[0])->getPredicate();
      for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
        if(VL0Pred!=dyn_cast<CmpInst>(VL[idx_vl])->getPredicate()){
          //not the same predicate
          BS.cancelScheduling(VL);
          newTreeEntry(VL,false);
          return;
        }
      }
      newTreeEntry(VL,true);
      for(int idx_operand=0;idx_operand<VL0->getNumOperands();idx_operand++){
        std::vector<Value*> operands;
        for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
          operands.push_back(dyn_cast<StoreInst>(VL[idx_vl])->getOperand(idx_operand));
        }
        buildTree_rec(operands,Depth+1);
      }
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
    case Instruction::Xor:{
      //build for each operand
      newTreeEntry(VL, true);
      for(int idx_operand = 0; idx_operand<VL0->getNumOperands();idx_operand++){
        std::vector<Value*> operands;
        for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
          operands.push_back(dyn_cast<Instruction>(VL[idx_vl])->getOperand(idx_operand));
        }
        buildTree_rec(operands,Depth+1);

      }

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
    case Instruction::AddrSpaceCast:{
      //ensure target type are the same. now we need to check the src type
      Type* SrcTy =VL0->getOperand(0)->getType();
      if(!isValidElementType(SrcTy)){
        BS.cancelScheduling(VL);
        newTreeEntry(VL, false);
        return;
      }
      for (int idx=1;idx<VL.size();idx++){
        Type* curr_type = dyn_cast<Instruction>(VL[idx])->getType();
        if((curr_type!=SrcTy)||(!isValidElementType(curr_type))){//not the same type or invalid vector type
          BS.cancelScheduling(VL);
          newTreeEntry(VL, false);
          return;
        }
      }
      newTreeEntry(VL, true);
      for(int idx_operand = 0; idx_operand<VL0->getNumOperands();idx_operand++){
        std::vector<Value*> operands;
        for (int idx_vl=0;idx_vl<VL.size();idx_vl++){
          operands.push_back(dyn_cast<Instruction>(VL[idx_vl])->getOperand(idx_operand));
        }
        buildTree_rec(operands,Depth+1);

      }
    }
    //do not deal with
    case Instruction::ExtractValue:
    case Instruction::ExtractElement:
    case Instruction::GetElementPtr:
    case Instruction::PHI:
    case Instruction::Call:
    case Instruction::ShuffleVector:{
      BS.cancelScheduling(VL);
      newTreeEntry(VL,false);//cannot schedule
      return;

    }
    default:{//TODO: what about alloca, atomic operations that bump into this scheme?
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

// Set insert point for IRBuilder
// after a list of instructions
void BoUpSLP::setInsertPointAfterBundle(ArrayRef<Value *> VL) {
  Instruction *VL0 = cast<Instruction>(VL[0]);
  Instruction *LastInst = getLastInstruction(VL);
  BasicBlock::iterator NextInst = LastInst;
  ++NextInst;
  Builder.SetInsertPoint(VL0->getParent(), NextInst);
  Builder.SetCurrentDebugLocation(VL0->getDebugLoc());
}

// gather a list of values into a vector
// If a value is in ScalarToTreeEntry, mark it with ExternalUse such that
// it will be extract later.
Value *BoUpSLP::Gather(ArrayRef<Value *> VL, VectorType *Ty) {
  Value *Vec = UndefValue::get(Ty);
  // Generate the 'InsertElement' instruction.
  for (unsigned i = 0; i < Ty->getNumElements(); ++i) {
    Vec = Builder.CreateInsertElement(Vec, VL[i], Builder.getInt32(i));
    if (Instruction *Insrt = dyn_cast<Instruction>(Vec)) {
      GatherSeq.insert(Insrt);
      CSEBlocks.insert(Insrt->getParent());

      // Add to our 'need-to-extract' list.
      if (ScalarToTreeEntry.count(VL[i])) {
        int Idx = ScalarToTreeEntry[VL[i]];
        TreeEntry *E = &VectorizableTree[Idx];
        // Find which lane we need to extract.
        int FoundLane = -1;
        for (unsigned Lane = 0, LE = VL.size(); Lane != LE; ++Lane) {
          // Is this the lane of the scalar that we are looking for ?
          if (E->Scalars[Lane] == VL[i]) {
            FoundLane = Lane;
            break;
          }
        }
        assert(FoundLane >= 0 && "Could not find the correct lane");
        ExternalUses.push_back(ExternalUser(VL[i], Insrt, FoundLane));
      }
    }
  }

  return Vec;
}

// Vectorize a list of values. If values are in the tree,
// vectorize the corresponding tree entry, otherwise,
// gather the values into a new vector.
Value *BoUpSLP::vectorizeTree(ArrayRef<Value *> VL) {
  if (ScalarToTreeEntry.count(VL[0])) {
    int Idx = ScalarToTreeEntry[VL[0]];
    TreeEntry *E = &VectorizableTree[Idx];
    if (E->isSame(VL))
      return vectorizeTree(E);
  }

  Type *ScalarTy = VL[0]->getType();
  if (StoreInst *SI = dyn_cast<StoreInst>(VL[0]))
    ScalarTy = SI->getValueOperand()->getType();
  VectorType *VecTy = VectorType::get(ScalarTy, VL.size());

  return Gather(VL, VecTy);
}

// Vectorize a single tree entry if it has not been vectorized.
// Currently, this function only handles Binary, Load/Store, and GEP OPs.
Value *BoUpSLP::vectorizeTree(TreeEntry *E) {
  IRBuilder<>::InsertPointGuard Guard(Builder);

  if (E->VectorizedValue) {
    DEBUG(dbgs() << "SLP: Diamond merged for " << *E->Scalars[0] << ".\n");
    return E->VectorizedValue;
  }

  Instruction *VL0 = cast<Instruction>(E->Scalars[0]);
  Type *ScalarTy = VL0->getType();
  if (StoreInst *SI = dyn_cast<StoreInst>(VL0))
    ScalarTy = SI->getValueOperand()->getType();
  VectorType *VecTy = VectorType::get(ScalarTy, E->Scalars.size());

  if (E->NeedToGather) {
    setInsertPointAfterBundle(E->Scalars);
    return Gather(E->Scalars, VecTy);
  }
  unsigned Opcode = getSameOpcode(E->Scalars);

  switch (Opcode) {

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
    case Instruction::Xor: {
      ValueList LHSVL, RHSVL;
      if (isa<BinaryOperator>(VL0) && VL0->isCommutative())
        reorderInputsAccordingToOpcode(E->Scalars, LHSVL, RHSVL);
      else
        for (int i = 0, e = E->Scalars.size(); i < e; ++i) {
          LHSVL.push_back(cast<Instruction>(E->Scalars[i])->getOperand(0));
          RHSVL.push_back(cast<Instruction>(E->Scalars[i])->getOperand(1));
        }

      setInsertPointAfterBundle(E->Scalars);

      Value *LHS = vectorizeTree(LHSVL);
      Value *RHS = vectorizeTree(RHSVL);

      if (LHS == RHS && isa<Instruction>(LHS)) {
        assert((VL0->getOperand(0) == VL0->getOperand(1)) && "Invalid order");
      }

      if (Value *V = alreadyVectorized(E->Scalars))
        return V;

      BinaryOperator *BinOp = cast<BinaryOperator>(VL0);
      Value *V = Builder.CreateBinOp(BinOp->getOpcode(), LHS, RHS);
      E->VectorizedValue = V;

      if (Instruction *I = dyn_cast<Instruction>(V))
        return propagateMetadata(I, E->Scalars);

      return V;
    }
    case Instruction::Load: {
      // Loads are inserted at the head of the tree because we don't want to
      // sink them all the way down past store instructions.
      setInsertPointAfterBundle(E->Scalars);

      LoadInst *LI = cast<LoadInst>(VL0);
      unsigned AS = LI->getPointerAddressSpace();

      Value *VecPtr = Builder.CreateBitCast(LI->getPointerOperand(),
                                            VecTy->getPointerTo(AS));
      unsigned Alignment = LI->getAlignment();
      LI = Builder.CreateLoad(VecPtr);
      if (!Alignment)
        Alignment = DL->getABITypeAlignment(LI->getPointerOperand()->getType());
      LI->setAlignment(Alignment);
      E->VectorizedValue = LI;
      return propagateMetadata(LI, E->Scalars);
    }
    case Instruction::Store: {
      StoreInst *SI = cast<StoreInst>(VL0);
      unsigned Alignment = SI->getAlignment();
      unsigned AS = SI->getPointerAddressSpace();

      ValueList ValueOp;
      for (int i = 0, e = E->Scalars.size(); i < e; ++i)
        ValueOp.push_back(cast<StoreInst>(E->Scalars[i])->getValueOperand());

      setInsertPointAfterBundle(E->Scalars);

      Value *VecValue = vectorizeTree(ValueOp);
      Value *VecPtr = Builder.CreateBitCast(SI->getPointerOperand(),
                                            VecTy->getPointerTo(AS));
      StoreInst *S = Builder.CreateStore(VecValue, VecPtr);
      if (!Alignment)
        Alignment = DL->getABITypeAlignment(SI->getPointerOperand()->getType());
      S->setAlignment(Alignment);
      E->VectorizedValue = S;
      return propagateMetadata(S, E->Scalars);
    }
    case Instruction::GetElementPtr: {
      setInsertPointAfterBundle(E->Scalars);

      ValueList Op0VL;
      for (int i = 0, e = E->Scalars.size(); i < e; ++i)
        Op0VL.push_back(cast<GetElementPtrInst>(E->Scalars[i])->getOperand(0));

      Value *Op0 = vectorizeTree(Op0VL);

      std::vector<Value *> OpVecs;
      for (int j = 1, e = cast<GetElementPtrInst>(VL0)->getNumOperands(); j < e;
           ++j) {
        ValueList OpVL;
        for (int i = 0, e = E->Scalars.size(); i < e; ++i)
          OpVL.push_back(cast<GetElementPtrInst>(E->Scalars[i])->getOperand(j));

        Value *OpVec = vectorizeTree(OpVL);
        OpVecs.push_back(OpVec);
      }

      Value *V = Builder.CreateGEP(Op0, OpVecs);
      E->VectorizedValue = V;

      if (Instruction *I = dyn_cast<Instruction>(V))
        return propagateMetadata(I, E->Scalars);

      return V;
    }
    default:
    llvm_unreachable("unknown inst");
  }
  return nullptr;
}

// Vectorize the tree from root recursively.
// external uses: some scalars are referenced by instructions not in the tree.
// extract them from vectors and replace the original use.
// Finally, remove use of all scalars in the tree such that those scalars can
// be removed safely.
Value *BoUpSLP::vectorizeTree() {
  Builder.SetInsertPoint(F->getEntryBlock().begin());
  vectorizeTree(&VectorizableTree[0]);

  DEBUG(dbgs() << "SLP: Extracting " << ExternalUses.size() << " values .\n");

  // Extract all of the elements with the external uses.
  for (UserList::iterator it = ExternalUses.begin(), e = ExternalUses.end();
       it != e; ++it) {
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
    if (isa<Instruction>(Vec)){
      if (PHINode *PH = dyn_cast<PHINode>(User)) {
        for (int i = 0, e = PH->getNumIncomingValues(); i != e; ++i) {
          if (PH->getIncomingValue(i) == Scalar) {
            Builder.SetInsertPoint(PH->getIncomingBlock(i)->getTerminator());
            Value *Ex = Builder.CreateExtractElement(Vec, Lane);
            CSEBlocks.insert(PH->getIncomingBlock(i));
            PH->setOperand(i, Ex);
          }
        }
      } else {
        Builder.SetInsertPoint(cast<Instruction>(User));
        Value *Ex = Builder.CreateExtractElement(Vec, Lane);
        CSEBlocks.insert(cast<Instruction>(User)->getParent());
        User->replaceUsesOfWith(Scalar, Ex);
     }
    } else {
      Builder.SetInsertPoint(F->getEntryBlock().begin());
      Value *Ex = Builder.CreateExtractElement(Vec, Lane);
      CSEBlocks.insert(&F->getEntryBlock());
      User->replaceUsesOfWith(Scalar, Ex);
    }

    DEBUG(dbgs() << "SLP: Replaced:" << *User << ".\n");
  }

  // For each vectorized value:
  for (int EIdx = 0, EE = VectorizableTree.size(); EIdx < EE; ++EIdx) {
    TreeEntry *Entry = &VectorizableTree[EIdx];

    // For each lane:
    for (int Lane = 0, LE = Entry->Scalars.size(); Lane != LE; ++Lane) {
      Value *Scalar = Entry->Scalars[Lane];
      // No need to handle users of gathered values.
      if (Entry->NeedToGather)
        continue;

      assert(Entry->VectorizedValue && "Can't find vectorizable value");

      Type *Ty = Scalar->getType();
      if (!Ty->isVoidTy()) {
        Value *Undef = UndefValue::get(Ty);
        Scalar->replaceAllUsesWith(Undef);
      }
      DEBUG(dbgs() << "SLP: \tErasing scalar:" << *Scalar << ".\n");
      cast<Instruction>(Scalar)->eraseFromParent();
    }
  }

  for (auto &BN : BlocksNumbers)
    BN.second.forget();

  Builder.ClearInsertionPoint();

  return VectorizableTree[0].VectorizedValue;
}

struct MySLPPass : public PassInfoMixin<MySLPPass>
{
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
               LoopInfo *LI_, DominatorTree *DT_,
               AssumptionCache *AC_, DemandedBits *DB_,
               OptimizationRemarkEmitter *ORE_)
  {
    const DataLayout *DL = &F.getParent()->getDataLayout();

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
        R.buildTree(seedPack);
      }
    }

    return false;
  }
  PreservedAnalyses run(Function &F,
                        FunctionAnalysisManager &FAM)
  {

    auto *SE = &FAM.getResult<ScalarEvolutionAnalysis>(F);
    auto *TTI = &FAM.getResult<TargetIRAnalysis>(F);
    auto *TLI = FAM.getCachedResult<TargetLibraryAnalysis>(F);
    auto *AA = &FAM.getResult<AAManager>(F);
    auto *LI = &FAM.getResult<LoopAnalysis>(F);
    auto *DT = &FAM.getResult<DominatorTreeAnalysis>(F);
    auto *AC = &FAM.getResult<AssumptionAnalysis>(F);
    auto *DB = &FAM.getResult<DemandedBitsAnalysis>(F);
    auto *ORE = &FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    bool Changed = runImpl(F, SE, TTI, TLI, AA, LI, DT, AC, DB, ORE);
    if (!Changed)
      return PreservedAnalyses::all();

    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    PA.preserve<AAManager>();
    PA.preserve<GlobalsAA>();
    return PA;
  }
};
} // end anonymous namespace

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
