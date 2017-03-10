CXX=~/research/datashield/bin/musl-clang++-debug-mask.py
#CXX=~/research/datashield/bin/musl-clang++-release-mask.py

test: test.o test2.o
	$(CXX) test.o test2.o -o test

test.o: test.cpp
	$(CXX) test.cpp -c

test2.o: test2.cpp
	$(CXX) test2.cpp -c 

clean:
	rm test test2.o test.o core_* *.ll
