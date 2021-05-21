#!/bin/sh
CC=~/research/datashield/ds_sysroot_release/bin/clang \
CFLAGS="-target x86_64-linux-musl -march=skylake -std=gnu99 -v -O3 -D__USE_DATASHIELD -fsanitize=safe-stack -mseparate-stack-seg -mllvm -datashield-library-mode -mllvm -datashield-modular" \
../musl/configure --prefix=~/research/datashield/ds_sysroot_release

