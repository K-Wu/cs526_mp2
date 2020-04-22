opt < tests-curated/arith-sub.ll -load build/pass/libSLPVectorizer-kdw.so -mtriple=x86_64-unknown -basicaa -slpvect-kdw -S  >tests-curated/Output/SingleCommand/arith-sub.ll.output  2>tests-curated/Output/SingleCommand/arith-sub.ll.output2
opt -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -S -mtriple=x86_64-unknown-linux -mcpu=corei7-avx -slp-threshold=-999 < tests-curated/broadcast.ll  >tests-curated/Output/SingleCommand/broadcast.ll.output  2>tests-curated/Output/SingleCommand/broadcast.ll.output2
opt < tests-curated/cast.ll -load build/pass/libSLPVectorizer-kdw.so -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7 -basicaa -slpvect-kdw -dce -S  >tests-curated/Output/SingleCommand/cast.ll.output  2>tests-curated/Output/SingleCommand/cast.ll.output2
opt < tests-curated/cmp_sel.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/cmp_sel.ll.output  2>tests-curated/Output/SingleCommand/cmp_sel.ll.output2
opt -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw < tests-curated/commutativity.ll -S  >tests-curated/Output/SingleCommand/commutativity.ll.output  2>tests-curated/Output/SingleCommand/commutativity.ll.output2
opt < tests-curated/different-vec-widths.ll -load build/pass/libSLPVectorizer-kdw.so -mattr=sse2 -slpvect-kdw -S  >tests-curated/Output/SingleCommand/different-vec-widths.ll.output  2>tests-curated/Output/SingleCommand/different-vec-widths.ll.output2
opt < tests-curated/external_user_jumbled_load.ll -load build/pass/libSLPVectorizer-kdw.so -S -mtriple=x86_64-unknown -mattr=+avx -slpvect-kdw  >tests-curated/Output/SingleCommand/external_user_jumbled_load.ll.output  2>tests-curated/Output/SingleCommand/external_user_jumbled_load.ll.output2
opt < tests-curated/external_user.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/external_user.ll.output  2>tests-curated/Output/SingleCommand/external_user.ll.output2
opt < tests-curated/extractelement.ll -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -S -mtriple=x86_64-unknown-linux -march=core-avx2  >tests-curated/Output/SingleCommand/extractelement.ll.output  2>tests-curated/Output/SingleCommand/extractelement.ll.output2
opt < tests-curated/extract.ll  -load build/pass/libSLPVectorizer-kdw.so-basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/extract.ll.output  2>tests-curated/Output/SingleCommand/extract.ll.output2
opt < tests-curated/extract-shuffle.ll -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -S -o - -mtriple=x86_64-unknown-linux -mcpu=bdver2 -slp-schedule-budget=1  >tests-curated/Output/SingleCommand/extract-shuffle.ll.output  2>tests-curated/Output/SingleCommand/extract-shuffle.ll.output2
opt < tests-curated/jumbled-load-multiuse.ll -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -S -mtriple=x86_64-unknown-linux -mattr=+sse4.2  >tests-curated/Output/SingleCommand/jumbled-load-multiuse.ll.output  2>tests-curated/Output/SingleCommand/jumbled-load-multiuse.ll.output2
opt < tests-curated/jumbled-load-shuffle-placement.ll -load build/pass/libSLPVectorizer-kdw.so -S -mtriple=x86_64-unknown -mattr=+avx -slpvect-kdw  >tests-curated/Output/SingleCommand/jumbled-load-shuffle-placement.ll.output  2>tests-curated/Output/SingleCommand/jumbled-load-shuffle-placement.ll.output2
opt < tests-curated/jumbled-load-used-in-phi.ll -load build/pass/libSLPVectorizer-kdw.so -S -mtriple=x86_64-unknown -mattr=+avx -slpvect-kdw  >tests-curated/Output/SingleCommand/jumbled-load-used-in-phi.ll.output  2>tests-curated/Output/SingleCommand/jumbled-load-used-in-phi.ll.output2
opt < tests-curated/load-bitcast-vec.ll -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -S -mtriple=x86_64-- -mattr=+avx   >tests-curated/Output/SingleCommand/load-bitcast-vec.ll.output  2>tests-curated/Output/SingleCommand/load-bitcast-vec.ll.output2
opt -load build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -slp-vectorize-hor -slp-vectorize-hor-store -S < tests-curated/load-merge.ll -mtriple=x86_64-apple-macosx -mcpu=haswell  >tests-curated/Output/SingleCommand/load-merge.ll.output  2>tests-curated/Output/SingleCommand/load-merge.ll.output2
opt < tests-curated/long_chains.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/long_chains.ll.output  2>tests-curated/Output/SingleCommand/long_chains.ll.output2
opt < tests-curated/multi_block.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/multi_block.ll.output  2>tests-curated/Output/SingleCommand/multi_block.ll.output2
opt < tests-curated/multi_user.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/multi_user.ll.output  2>tests-curated/Output/SingleCommand/multi_user.ll.output2
opt < tests-curated/odd_store.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/odd_store.ll.output  2>tests-curated/Output/SingleCommand/odd_store.ll.output2
opt < tests-curated/phi3.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -dce -S -mtriple=x86_64-apple-macosx10.8.0 -mcpu=corei7  >tests-curated/Output/SingleCommand/phi3.ll.output  2>tests-curated/Output/SingleCommand/phi3.ll.output2
opt < tests-curated/phi.ll -load build/pass/libSLPVectorizer-kdw.so -basicaa -slpvect-kdw -slp-threshold=-100 -dce -S -mtriple=i386-apple-macosx10.8.0 -mcpu=corei7-avx  >tests-curated/Output/SingleCommand/phi.ll.output  2>tests-curated/Output/SingleCommand/phi.ll.output2