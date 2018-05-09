#CC=~/research/datashield/ds_sysroot_debug/bin/clang
#CC=~/research/datashield/bin/musl-clang-debug-mask.py
#CC=~/research/datashield/bin/musl-clang-release-mask.py
CC=~/research/datashield/bin/musl-clang-debug-mpx.py

.PHONY: all
all: injector-pipe test-const-noinit-64
	./injector-pipe test-const-noinit-64 yolkData
	./test-const-noinit-64

injector-pipe: injector-pipe.cpp
	g++ $^ -o $@

test-const-noinit-64: test-const-noinit-64.o
	$(CC) $(CFLAGS) $^ -o $@

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

clean:
	rm -f test-const-noinit-64 *.o

