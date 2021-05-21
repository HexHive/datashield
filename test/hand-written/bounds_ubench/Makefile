#CC=~/research/datashield/ds_sysroot_debug/bin/clang
#CC=~/research/datashield/bin/musl-clang-debug-mask.py
#CC=~/research/datashield/bin/musl-clang-release-mask.py
CC=~/research/datashield/bin/musl-clang-debug-mpx.py

#CFLAGS = -march=skylake -mtune=skylake -fsanitize=safe-stack -mseparate-stack-seg
CFLAGS = -ggdb -Wl,-mllvm,-debug-only=datashield -O3

all: bounds_ubench
	nice -15 ./bounds_ubench

bounds_ubench: bounds_ubench.o
	$(CC) $(CFLAGS) $^ -o $@

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

clean:
	rm -f bounds_ubench *.o

