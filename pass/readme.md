
```
void collectStores(){
    //find all stores
    //find continuous access 
    //group as chunks
}
```

```
void buildTree_rec(){
//if recursion reaches max depth, stop
// Don't handle vectors.
// Don't handle vectors (in store instructions).
// Don't vectorize if the instructions are not in the same block or same OP.
// Check if this is a duplicate of another scalar in the tree.
// Check that none of the instructions in the bundle are already in the tree.
// check block reachability
// check no duplication in this bundle
// check scheduling
// use struct BlockScheduling instead
// -deleted- .1 check reachable from the entry
// -deleted- .2 check scheduling
// process according to the Op

    //.1 load check isSimple check consecutive 
    //.1.1 store check consecutive
    //.2 zext, sext, bitcast etc. same srctype. isElementType. //Trunc, ZExt, SExt, FPTrunc, FPToUI, FPToSI, FPExt, UIToFP, SIToFP, PtrToInt, IntToPtr, BitCast. AddrSpaceCast
    //.3 icmp, fcmp check predicate
    //.4 binary operator + select build for each operand (in the future: refine order if commutative)

    //don't deal with the following:
        //what for case Instruction::ExtractValue:
        //case Instruction::ExtractElement:

        //Instruction::GetElementPtr: in the future, the easiest way is to deal with only the (pointer operand, constant offset) and constant offset are consecutive. consecutiveaccess is not valid for this.

        //what for case Instruction::PHI

        //what for case case Instruction::ExtractElement: 

        case Instruction::Call:
        case Instruction::ShuffleVector: {
}
```

```
void buildTree(){
    //recursively execute buildTree_rec()
    //gather external uses (vectorize tree needs to and only needs to generate ExtractElements for (scalar, user) pair in ExternalUses)
    // - don't need to care about in-tree uses as long as both of the use and usee are vectorized

}
```

```
void getCost(){
    //if vectorTree size ==2 then neither can be NeedToGather
    //
}
```