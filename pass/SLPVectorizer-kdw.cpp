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


using namespace llvm;
#define DEBUGKWUx
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



namespace
{
struct MySLPPass : public PassInfoMixin<MySLPPass>
{
  bool runImpl(Function &F, ScalarEvolution *SE_,
                                TargetTransformInfo *TTI_,
                                TargetLibraryInfo *TLI_, AliasAnalysis *AA_,
                                LoopInfo *LI_, DominatorTree *DT_,
                                AssumptionCache *AC_, DemandedBits *DB_,
                                OptimizationRemarkEmitter *ORE_) {

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