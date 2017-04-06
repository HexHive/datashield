DS_HOME=$HOME/research/datashield
DS_SYSROOT=$HOME/research/datashield/ds_sysroot_baseline
GCC_LIBDIR=$(dirname $(gcc -print-libgcc-file-name))
cmake -GNinja -DLLVM_PATH=$DS_HOME/compiler/llvm-3.9 \
-DCMAKE_SYSROOT=$DS_SYSROOT \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_CXX_COMPILER=$DS_HOME/compiler/build/bin/clang++ \
-DCMAKE_C_COMPILER=$DS_HOME/compiler/build/bin/clang \
-DCMAKE_CXX_FLAGS="-v -O3 -nostdlib" \
-DCMAKE_C_FLAGS="-v -O3 -nostdlib"  \
-DCMAKE_INSTALL_PREFIX=$DS_SYSROOT \
-DLIBCXX_CXX_ABI_INCLUDE_PATHS=$DS_HOME/libcxx/libcxxabi/include \
-DLIBCXX_CXX_ABI=libcxxabi \
-DLIBCXX_CXX_ABI_LIBRARY_PATH=$DS_SYSROOT/lib \
-DLIBCXX_HAS_MUSL_LIBC=ON \
-DLIBCXX_HAS_GCC_S_LIB=OFF \
-DLIBCXX_ENABLE_SHARED=OFF \
-DCMAKE_STATIC_LINKER_FLAGS="$GCC_LIBDIR/crtbegin.o $GCC_LIBDIR/crtend.o" \
-DCMAKE_MODULE_LINKER_FLAGS="$GCC_LIBDIR/crtbegin.o $GCC_LIBDIR/crtend.o" \
../libcxx

