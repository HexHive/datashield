#CC=~/research/datashield/ds_sysroot_debug/bin/clang
#CC=~/research/datashield/bin/musl-clang-debug-mask.py
#CC=~/research/datashield/bin/musl-clang-release-mask.py
CC=~/research/datashield/bin/musl-clang-debug-mpx.py

various-code: various-code.o
	$(CC) $(CFLAGS) $^ -o $@

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

clean:
	rm -f various-code *.o

