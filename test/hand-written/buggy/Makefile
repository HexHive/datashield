CC=~/research/datashield/bin/musl-clang-debug-mask.py
#CC=~/research/datashield/bin/musl-clang-release-mask.py
#CC=~/research/datashield/bin/musl-clang-debug-late-mpx.py

test: test.o 
	$(CC) test.c -o test

clean:
	rm test core_* *.ll
