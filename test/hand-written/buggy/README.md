# buggy

This test/example shows how to use annotations and can be used to verify that the bounds checks are working correctly.

The input to test is two positive integers.  The first integer (n) is the size
of the array of sensitive objects to create.  The second integer is the index
of the integer (m) to print out after sorting the array of objects.  
If `m >= n` then there should be a bounds error and the program should abort.

Example:

    ./test 100 42 # no bounds error
    ./test 100 101 # bounds error!
