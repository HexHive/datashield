#!/bin/sh
CC=~/research/datashield/compiler/build/bin/clang \
CFLAGS="-v -O3" \
../musl/configure --prefix=~/research/datashield/ds_sysroot_baseline

