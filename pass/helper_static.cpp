#pragma once


/// \returns the AA location that is being access by the instruction.
static MemoryLocation getLocation(Instruction *I, AliasAnalysis *AA) {
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return MemoryLocation::get(SI);
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return MemoryLocation::get(LI);
  return MemoryLocation();
}
