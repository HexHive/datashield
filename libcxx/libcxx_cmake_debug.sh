DS_HOME=$HOME/research/datashield
DS_SYSROOT=$HOME/research/datashield/ds_sysroot_debug
GCC_LIBDIR=$(dirname $(gcc -print-libgcc-file-name))
cmake -GNinja -DLLVM_PATH=$DS_HOME/compiler/llvm \
-DCMAKE_SYSROOT=$DS_SYSROOT \
-DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_CXX_COMPILER=$DS_SYSROOT/bin/clang++ \
-DCMAKE_C_COMPILER=$DS_SYSROOT/bin/clang \
-DCMAKE_CXX_FLAGS="-v -fstandalone-debug -nostdlib -mllvm -datashield-modular -mllvm -datashield-library-mode $DS_SYSROOT/lib/libc.a" \
-DCMAKE_C_FLAGS="-v -fstandalone-debug -nostdlib -mllvm -datashield-modular -mllvm -datashield-library-mode $DS_SYSROOT/lib/libc.a"  \
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

