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
#include <vector>
#include <unordered_map>


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


void BoUpSLP::buildTree(ArrayRef<Value *> Roots){
dbg_executes(errs()<<"WARNING: STOREVL: ";);
        for (auto siter = Roots.begin(); siter!=Roots.end(); siter++){
          dbg_executes(errs()<<*siter<<", ";);
        }
        dbg_executes(errs()<<"\n";);
}

void BoUpSLP::buildTree_rec(ArrayRef<Value *> Roots, unsigned Depth){

}

struct MySLPPass : public PassInfoMixin<MySLPPass>
{
  std::vector<std::vector<Value*>> collectStores(BasicBlock* BB, BoUpSLP &R){
    std::vector<std::vector<Value*>> storePacks;

    std::vector<Value*> storeInsts;
    std::vector<Value*> heads;
    std::unordered_map<Value*,Value*> tail_to_head_map;
    std::unordered_map<Value*, Value*> head_to_tail_map;
    std::unordered_map<Value*, bool> visited_heads;

    //find out all the store instructions in this basic block
    for(BasicBlock::iterator iter=BB->begin();iter!=BB->end();iter++){
      if (StoreInst* SI = dyn_cast<StoreInst>(iter)){
        storeInsts.push_back(SI);
      }
    }

    //O(N^2) brute force find consecutive store pairs
    for (int idx1=0;idx1<storeInsts.size();idx1++){
      for (int idx2=0;idx2<storeInsts.size();idx2++){
        if (idx1==idx2)
          continue;
        if(R.isConsecutiveAccess(storeInsts[idx1],storeInsts[idx2])){
          heads.push_back(storeInsts[idx1]);
          head_to_tail_map[storeInsts[idx1]]=storeInsts[idx2];
          tail_to_head_map[storeInsts[idx2]]=storeInsts[idx1];
          visited_heads[storeInsts[idx1]]=false;
        }
      }
    }

    //connect consecutive store pairs into packs
    for (int idx=0;idx<heads.size();idx++){
      if(visited_heads[heads[idx]])
        continue;
      //find the very beginning store instruction
      Value* beginInst = heads[idx];
      while(tail_to_head_map.count(beginInst)!=0)
        beginInst=tail_to_head_map[beginInst];
      if(!beginInst)
        dbg_executes(errs()<<"FUCK!\n";);
      std::vector<Value*> storePack;
      storePack.push_back(beginInst);
      visited_heads[beginInst]=true;
      while(head_to_tail_map.count(beginInst)!=0){
        beginInst=head_to_tail_map[beginInst];
        storePack.push_back(beginInst);
        visited_heads[beginInst]=true;
      }
      dbg_executes(errs()<<"WARNING: STOREVAL IN COLLECTSTORES: ";);
      for (int idx1=0;idx1<storePack.size();idx1++){
        dbg_executes(errs()<<storePack[idx1]<<",";);
      }
      dbg_executes(errs()<<"\n";);
      storePacks.push_back(storePack);
    }


    return storePacks;

  }

  bool runImpl(Function &F, ScalarEvolution *SE_,
                                TargetTransformInfo *TTI_,
                                TargetLibraryInfo *TLI_, AliasAnalysis *AA_,
                                LoopInfo *LI_, DominatorTree *DT_,
                                AssumptionCache *AC_, DemandedBits *DB_,
                                OptimizationRemarkEmitter *ORE_) {
    const DataLayout *DL= &F.getParent()->getDataLayout();

    BoUpSLP R(&F, SE_, DL, TTI_, TLI_, AA_, LI_, DT_);
    for (po_iterator<BasicBlock*> it = po_begin(&F.getEntryBlock());it!=po_end(&F.getEntryBlock());it++){
      BasicBlock* bb = *it;
      //dbg_executes(errs()<<"WARNING: "<<*bb<<"\n";);
      std::vector<std::vector<Value*>> StoreVLs = collectStores(bb, R);
      for (ArrayRef<Value*> StoreVL:StoreVLs){
        
        R.buildTree(StoreVL);
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