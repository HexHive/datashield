DS_HOME=$HOME/research/datashield
DS_SYSROOT=$DS_HOME/ds_sysroot_release
cmake -GNinja -DLLVM_PATH=$DS_HOME/compiler/llvm-3.9 \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_C_FLAGS="-O3 -mllvm -datashield-modular -mllvm -datashield-library-mode -nostdlib $DS_SYSROOT/lib/libc.a"  \
-DCMAKE_CXX_FLAGS="-O3 -mllvm -datashield-modular -mllvm -datashield-library-mode -nostdlib $DS_SYSROOT/lib/libc.a"  \
-DCMAKE_CXX_COMPILER=$DS_HOME/compiler/build/bin/clang++ \
-DCMAKE_C_COMPILER=$DS_HOME/compiler/build/bin/clang \
-DCMAKE_INSTALL_PREFIX=$DS_SYSROOT \
-DLIBCXXABI_LIBCXX_PATH=$DS_HOME/libcxx/libcxx \
-DLIBCXXABI_LIBCXX_INCLUDES=$DS_HOME/libcxx/libcxx/include \
-DLIBCXXABI_LIBUNWIND_PATH=$DS_HOME/libcxx/libunwind \
-DLIBCXXABI_USE_LLVM_UNWINDER=ON \
-DLIBCXXABI_ENABLE_SHARED=OFF \
../libcxxabi
