#!/bin/sh
CC=~/research/datashield/ds_sysroot_debug/bin/clang \
CFLAGS="-target x86_64-linux-musl -march=skylake -std=gnu99 -v -g -O0 -D__USE_DATASHIELD -DDEBUG_MODE -fsanitize=safe-stack -mseparate-stack-seg -mllvm -datashield-library-mode -mllvm -datashield-modular" \
../musl/configure --enable-debug --prefix=~/research/datashield/ds_sysroot_debug
