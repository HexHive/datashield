DS_HOME=$HOME/research/datashield 
DS_SYSROOT=$HOME/research/datashield/ds_sysroot_baseline
cmake -GNinja -DLLVM_PATH=$DS_HOME/compiler/llvm-3.9 \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_C_FLAGS="-O3" \
-DCMAKE_CXX_FLAGS="-O3" \
-DCMAKE_CXX_COMPILER=$DS_HOME/compiler/build/bin/clang++ \
-DCMAKE_C_COMPILER=$DS_HOME/compiler/build/bin/clang \
-DCMAKE_INSTALL_PREFIX=$DS_SYSROOT \
../libunwind
