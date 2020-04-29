#!/bin/sh
opt < tests/add.ll  -passes="slpvect-kdw" -verify -load-pass-plugin=tests/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/add.ll.output 2>tests/Output/add.ll.output2
#opt < tests/simpletest.ll  -passes="slpvect-kdw" -verify -load-pass-plugin=tests/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/simpletest.ll.output 2>tests/Output/simpletest.ll.output2
#opt < tests/simpletest2.ll  -passes="slpvect-kdw" -verify -load-pass-plugin=tests/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/simpletest2.ll.output 2>tests/Output/simpletest2.ll.output2
#opt < tests/simpletest3.ll  -passes="slpvect-kdw" -verify -load-pass-plugin=tests/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/simpletest3.ll.output 2>tests/Output/simpletest3.ll.output2
#opt < tests/simpletest4.ll  -passes="slpvect-kdw" -verify -load-pass-plugin=tests/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/simpletest4.ll.output 2>tests/Output/simpletest4.ll.output2
#opt < tests/test5.ll  -passes="slpvect-kdw" -verify -load-pass-plugin=tests/../build/pass/libSLPVectorizer-kdw.so -S >tests/Output/test5.ll.output 2>tests/Output/test5.ll.output2