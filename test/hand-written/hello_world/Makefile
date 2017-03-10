CC=~/research/datashield/bin/musl-clang-debug-mask.py
#CC=~/research/datashield/bin/musl-clang-debug-late-mpx.py

test: test.o test2.o
	$(CC) test.o test2.o -o test

test.o: test.c
	$(CC) test.c -c

test2.o: test2.c
	$(CC) test2.c -c 

clean:
	rm test test2.o test.o core_* *.ll
