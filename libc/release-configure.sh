#!/bin/sh
CC=~/research/datashield/ds_sysroot_release/bin/clang \
CFLAGS="-std=gnu99 -v -O3 -D__USE_DATASHIELD -mllvm -datashield-library-mode -mllvm -datashield-modular" \
../musl/configure --prefix=~/research/datashield/ds_sysroot_release

