
# CS526 SP20 MP1
Kun Wu \<netid: kunwu2, UIN: 676032253\>
Dawei Sun \<netid: daweis2, UIN: \>
## Build
This project uses another skeleton code from one online tutorial \[1\] than the one provided by CS 526 Spring 2020, in order to enable building this project as a shared library. It uses cmake as its build management system, and adopts the new PassManager. The provided skeleton code mandates the project building with llvm-8.0.1 source tree as long as built-in passes are called in the legacy pass manager, which forbids quick development.

### Prerequisite
cmake 3.1 is required. Please modify the ENV{CC} and ENV{CXX} in /CmakeLists.txt according to the gcc binaries location on your computer. A least version of gcc is mandated by llvm-8.0.1.

### Build Command
```
mkdir build
cd build && rm -rf * && cmake .. && make && cd ..
```

### Pass Invocation Example
Be sure to use the following as `opt` option:

- `-load-pass-plugin=/path/to/libSLPVectorizer-kdw.so` to load the compiled shared memory
- `-passes="slpvect-kdw"` to invoke my pass

```
opt < tests/simpletest.ll  -passes="slpvect-kdw" -load-pass-plugin=tests-ourpass/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/simpletest.ll.output 2>tests/Output/simpletest.ll.output2
```

## Result of SLPVectorizer in LLVM 8.0.1
```
[kunwu2@sp20-cs526-10 cs526_mp2]$ bash tests-original/opt-commands-with-redirection.txt 
tests-original/opt-commands-with-redirection.txt: line 57:  6401 Aborted                 (core dumped) opt -basicaa -slp-vectorizer -dce -S -mtriple=x86_64-unknown-linux-gnu < tests-original/X86/crash_gep.ll > tests-original/RefResults/X86/crash_gep.ll.output 2> tests-original/RefResults/X86/crash_gep.ll.output2
[kunwu2@sp20-cs526-10 cs526_mp2]$
```

```
[kunwu2@sp20-cs526-10 cs526_mp2]$ bash tests-original/opt-commands-with-redirection-avx2.txt 
[kunwu2@sp20-cs526-10 cs526_mp2]$
```

## Tests
See make-and-test.sh for test command.

/tests/ contain four very simple test cases which tests whether the pass can correctly handle simple and complicated situations, such as nested structure, multi-dimensional array, array in structure, and (U1) load/store, etc. Eventually most scalar variables and all its dependency, i.e., getelementptr instructions to get that, should be eliminated after the nested scalar is promoted. 

Some of the test cases use cases from External Test Cases as skeleton but have been modified accordingly to suit my test demands.

```
[kunwu2@sp20-cs526-10 cs526_mp2]$ lit -v tests/.
-- Testing: ??? tests, ??? threads --
PASS: ???
???
Testing Time: ???s
  Expected Passes    : ???
```

## A Curated Set of Tests
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

## External Test Cases
/tests-original/ and its derivation /tests-ourpass/ are from the llvm-project github repo \[13\], specifically at <https://github.com/llvm/llvm-project/tree/master/llvm/test/Transforms/SLPVectorizer>.

### Results
The implementation only passes ??? out of ??? tests in /tests-ourpass/*.ll. The most prominent issues come from 1)???, 2)???, such as ???, not supported, and 3)???. However, they are expected behavior of the implemented logic.

```
********************
Failing Tests (???):
    ???

  Expected Passes    : ???
  Unexpected Failures: ???
  ```

## Next Steps
According to the external test cases result, ??? is necessary to make our ??? pass more complete and closer to the official ???.

1) add support to ???
2) ???
3) ???

??? is not considered in the next steps as it ???.

## Reference
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