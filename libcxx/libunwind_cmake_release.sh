DS_HOME=$HOME/research/datashield 
DS_SYSROOT=$HOME/research/datashield/ds_sysroot_release 
cmake -GNinja -DLLVM_PATH=$DS_HOME/compiler/llvm \
-DCMAKE_C_FLAGS="-O3 -mllvm -datashield-modular -mllvm -datashield-library-mode -nostdlib $DS_SYSROOT/lib/libc.a"  \
-DCMAKE_CXX_FLAGS="-O3 -mllvm -datashield-modular -mllvm -datashield-library-mode -nostdlib $DS_SYSROOT/lib/libc.a"  \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_CXX_COMPILER=$DS_SYSROOT/bin/clang++ \
-DCMAKE_C_COMPILER=$DS_SYSROOT/bin/clang \
-DCMAKE_INSTALL_PREFIX=$DS_SYSROOT \
-DLIBUNWIND_ENABLE_SHARED=OFF \
../libunwind
