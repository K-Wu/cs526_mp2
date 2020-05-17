
# CS526 SP20 MP2
Kun Wu \<netid: kunwu2, UIN: 676032253\>

Dawei Sun \<netid: daweis2, UIN: \>
## Build
This project uses skeleton code from one online tutorial \[1\] other than the one provided by CS 526 Spring 2020, in order to enable building this project as a shared library. It uses cmake as its build management system, and adopts the new PassManager. But after trial, we found the new pass manager cannot correctly invoke AA->alias(), in which case the latter always return MayAlias. This defeats the BlockScheduling algorithm. We still use this skeleton code but switched to the legacy pass manager. It succeessfully compiled.

### Prerequisite
cmake 3.1 is required. Please modify the ENV{CC} and ENV{CXX} in /CmakeLists.txt according to the gcc binaries location on your computer. A least version of gcc is mandated by llvm-8.0.1.

### Build Steps
1. modify env.sh accordingly and execute `source env.sh`.
2. execute `mkdir build`
3. execute `cd build && rm -rf * && cmake .. && make && cd ..`

### Pass Invocation Example
Be sure to use the following as `opt` option:

- `-load /path/to/libSLPVectorizer-kdw.so` to load the compiled shared memory
- `-slpvect-kdw` to invoke my pass

```
opt < tests/simpletest.ll  -passes="slpvect-kdw" -load-pass-plugin=tests-ourpass/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/simpletest.ll.output 2>tests/Output/simpletest.ll.output2
```

## Correctness Test Cases
tests in /tests-original/ and its derivation in /tests-curated/ and /tests-curated-refresults/ are from the llvm-project github repo \[13\], specifically at <https://github.com/llvm/llvm-project/tree/master/llvm/test/Transforms/SLPVectorizer>.

### A Curated Set of Tests
They are placed in tests-curated/. The reference results are in tests-curated-refresults/.

1. X86/arith-sub.ll
2. X86/broadcast.ll
3. X86/cmp_sel.ll
4. X86/commutativity.ll
5. X86/extract-shuffle.ll
6. X86/extract.ll
7. X86/external_user.ll
8. X86/external_user_jumbled_load.ll
9. X86/different-vec-widths.ll
10. X86/jumbled-load-shuffle-placement.ll
11. X86/jumbled-load-multiuse.ll
12. X86/jumbled-load-used-in-phi.ll
13. X86/load-bitcast-vec.ll
14. X86/load-merge.ll
15. X86/long_chains.ll
16. X86/multi_user.ll
17. X86/multi_block.ll
18. X86/phi.ll
19. X86/phi3.ll
20. X86/odd_store.ll
21. X86/extractelement.ll
22. X86/cast.ll

```
[kunwu2@sp20-cs526-10 cs526_mp2]$ lit -v tests-curated/.
...
********************
Testing Time: 0.63s
********************
Failing Tests (18):
    LIT LLVM kdw SLP test cases :: arith-sub.ll
    LIT LLVM kdw SLP test cases :: broadcast.ll
    LIT LLVM kdw SLP test cases :: cast.ll
    LIT LLVM kdw SLP test cases :: commutativity.ll
    LIT LLVM kdw SLP test cases :: different-vec-widths.ll
    LIT LLVM kdw SLP test cases :: external_user.ll
    LIT LLVM kdw SLP test cases :: external_user_jumbled_load.ll
    LIT LLVM kdw SLP test cases :: extract.ll
    LIT LLVM kdw SLP test cases :: extractelement.ll
    LIT LLVM kdw SLP test cases :: jumbled-load-multiuse.ll
    LIT LLVM kdw SLP test cases :: jumbled-load-shuffle-placement.ll
    LIT LLVM kdw SLP test cases :: jumbled-load-used-in-phi.ll
    LIT LLVM kdw SLP test cases :: load-merge.ll
    LIT LLVM kdw SLP test cases :: long_chains.ll
    LIT LLVM kdw SLP test cases :: multi_block.ll
    LIT LLVM kdw SLP test cases :: multi_user.ll
    LIT LLVM kdw SLP test cases :: phi.ll
    LIT LLVM kdw SLP test cases :: phi3.ll

  Expected Passes    : 4
  Unexpected Failures: 18
```

## Performance Benchmark
A set of performance benchmarks from openbenchmark \[19\] are set up in https://github.com/K-Wu/openbenchmark_llvm_vectorizer. They include vectorization-enabled single-core programs and multithreading programs, either enabled by openmp or pthread.

Use the following command to retrieve the submodule:

```
git submodule update --init --recursive
```

### Performance Benchmark Compilation Optimization Pipeline
We obtained the original O3 optimization pipeline in LLVM 8.0.1 and replace the `-slp-vectorizer` with our pass. The flags to compile with the new O3 optimization pipeline is:

```
opt -load /path/to/libSLPVectorizer-kdw.so -tti -tbaa -scoped-noalias -assumption-cache-tracker -targetlibinfo -verify -ee-instrument -simplifycfg -domtree -sroa -early-cse -lower-expect -targetlibinfo -tti -tbaa -scoped-noalias -assumption-cache-tracker -profile-summary-info -forceattrs -inferattrs -domtree -callsite-splitting -ipsccp -called-value-propagation -globalopt -domtree -mem2reg -deadargelim -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -simplifycfg -basiccg -globals-aa -prune-eh -inline -functionattrs -argpromotion -domtree -sroa -basicaa -aa -memoryssa -early-cse-memssa -speculative-execution -basicaa -aa -lazy-value-info -jump-threading -correlated-propagation -simplifycfg -domtree -aggressive-instcombine -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -libcalls-shrinkwrap -loops -branch-prob -block-freq -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -pgo-memop-opt -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -tailcallelim -simplifycfg -reassociate -domtree -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -loop-rotate -licm -loop-unswitch -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -indvars -loop-idiom -loop-deletion -loop-unroll -mldst-motion -phi-values -basicaa -aa -memdep -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -gvn -phi-values -basicaa -aa -memdep -memcpyopt -sccp -demanded-bits -bdce -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -lazy-value-info -jump-threading -correlated-propagation -basicaa -aa -phi-values -memdep -dse -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -licm -postdomtree -adce -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -barrier -elim-avail-extern -basiccg -rpo-functionattrs -globalopt -globaldce -basiccg -globals-aa -float2int -domtree -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -loop-rotate -loop-accesses -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -loop-distribute -branch-prob -block-freq -scalar-evolution -basicaa -aa -loop-accesses -demanded-bits -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -loop-vectorize -loop-simplify -scalar-evolution -aa -loop-accesses -loop-load-elim -basicaa -aa -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -simplifycfg -domtree -loops -scalar-evolution -basicaa -aa -demanded-bits -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -slpvect-kdw -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -loop-unroll -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -licm -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -transform-warning -alignment-from-assumptions -strip-dead-prototypes -globaldce -constmerge -domtree -loops -branch-prob -block-freq -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -branch-prob -block-freq -loop-sink -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instsimplify -div-rem-pairs -simplifycfg -disable-output
```

## Command to Obtain O3 Optimization Pipeline
```
[#####@######## ###########]$ llvm-as </dev/null | opt -O3 -disable-output -debug-pass=Arguments
Pass Arguments:  -tti -tbaa -scoped-noalias -assumption-cache-tracker -targetlibinfo -verify -ee-instrument -simplifycfg -domtree -sroa -early-cse -lower-expect
Pass Arguments:  -targetlibinfo -tti -tbaa -scoped-noalias -assumption-cache-tracker -profile-summary-info -forceattrs -inferattrs -domtree -callsite-splitting -ipsccp -called-value-propagation -globalopt -domtree -mem2reg -deadargelim -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -simplifycfg -basiccg -globals-aa -prune-eh -inline -functionattrs -argpromotion -domtree -sroa -basicaa -aa -memoryssa -early-cse-memssa -speculative-execution -basicaa -aa -lazy-value-info -jump-threading -correlated-propagation -simplifycfg -domtree -aggressive-instcombine -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -libcalls-shrinkwrap -loops -branch-prob -block-freq -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -pgo-memop-opt -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -tailcallelim -simplifycfg -reassociate -domtree -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -loop-rotate -licm -loop-unswitch -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -indvars -loop-idiom -loop-deletion -loop-unroll -mldst-motion -phi-values -basicaa -aa -memdep -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -gvn -phi-values -basicaa -aa -memdep -memcpyopt -sccp -demanded-bits -bdce -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -lazy-value-info -jump-threading -correlated-propagation -basicaa -aa -phi-values -memdep -dse -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -licm -postdomtree -adce -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -barrier -elim-avail-extern -basiccg -rpo-functionattrs -globalopt -globaldce -basiccg -globals-aa -float2int -domtree -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -loop-rotate -loop-accesses -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -loop-distribute -branch-prob -block-freq -scalar-evolution -basicaa -aa -loop-accesses -demanded-bits -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -loop-vectorize -loop-simplify -scalar-evolution -aa -loop-accesses -loop-load-elim -basicaa -aa -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -simplifycfg -domtree -loops -scalar-evolution -basicaa -aa -demanded-bits -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -slp-vectorizer -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -loop-unroll -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -licm -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -transform-warning -alignment-from-assumptions -strip-dead-prototypes -globaldce -constmerge -domtree -loops -branch-prob -block-freq -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -branch-prob -block-freq -loop-sink -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instsimplify -div-rem-pairs -simplifycfg -verify
Pass Arguments:  -domtree
Pass Arguments:  -targetlibinfo -domtree -loops -branch-prob -block-freq
Pass Arguments:  -targetlibinfo -domtree -loops -branch-prob -block-freq
```

## Helpful Articles for This Pass Development
\[1\] Writing LLVM Pass in 2018 <https://medium.com/@mshockwave/writing-llvm-pass-in-2018-part-i-531c700e85eb> 

\[2\] MIT 6.172 Performance Engineering of Software Systems Notes <https://ocw.mit.edu/courses/electrical-engineering-and-computer-science/6-172-performance-engineering-of-software-systems-fall-2018/lecture-slides/MIT6_172F18_lec9.pdf> 

\[3\] Deep Dive into LLVM Passes <http://legup.eecg.utoronto.ca/wiki/lib/exe/fetch.php?media=passes_sra.pdf> 

\[4\] \[llvm-dev\] Guidelines for pass initialization <http://lists.llvm.org/pipermail/llvm-dev/2015-August/089500.html> 

\[5\] LLVM Class Reference<http://legup.eecg.utoronto.ca/doxygen/annotated.html> 

\[6\] llvm:Value Class Reference <http://legup.eecg.utoronto.ca/doxygen/classllvm_1_1Value.html> 

\[7\] Clang Class Hierarchy <https://clang.llvm.org/doxygen/inherits.html> 

\[8\] A Quick Introduction to Classical Compiler Design <http://www.aosabook.org/en/llvm.html> 

\[9\] Introduction to LLVM (II) <http://www.cs.toronto.edu/~pekhimenko/courses/cscd70-w18/docs/Tutorial%202%20-%20Intro%20to%20LLVM%20(Cont).pdf>

\[10\] lit - LLVM Integrated Tester <https://llvm.org/docs/CommandGuide/lit.html>

\[11\] LLVM Testing Infrastructure Guide <https://llvm.org/docs/TestingGuide.html>

\[12\] Writing an LLVM Pass <http://laure.gonnord.org/pro/research/ER03_2015/lab3_intro.pdf>

\[13\] Github: llvm-project/llvm/test/ <https://github.com/llvm/llvm-project/tree/master/llvm/test>

\[14\] Debug Info Tutorial <https://llvm.org/devmtg/2014-10/Slides/Christopher-DebugInfoTutorial.pdf>

\[15\] LLVM, in Greater Detail <http://www.cs.cmu.edu/afs/cs/academic/class/15745-s13/public/lectures/L6-LLVM-Detail.pdf>

\[16\] LLVM Language Reference Manual <https://llvm.org/docs/LangRef.html>

\[17\] Vasileios Porpodas et al. Look-Ahead SLP: Auto-Vectorization in the Presence of Commutative Operations.  <http://vporpo.me/papers/vwslp_pact2018_slides.pdf>

\[18\] Vasileios Porpodas and Timothy M. Jones. TSLP Throttling Automatic Vectorization: When Less is More. <https://llvm.org/devmtg/2015-10/slides/Porpodas-ThrottlingAutomaticVectorization.pdf>

\[19\] OpenBenchmark Test on SLPVectorizer in LLVM 3.4 <https://openbenchmarking.org/result/1307291-SO-FSLPVECTO83>