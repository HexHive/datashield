# Datashield

Note that this is simply a research demonstration, and it should not be used in any production environment or to process sensitive data.

There are known limitations in the compiler extensions that result in incomplete security coverage due to bounds not being
generated for certain program elements.  The compiler outputs a list of such elements in a file named <module>__infiniteBoundsList
in the same directory as the source file if it is possible to create a file in that directory.  If it is not possible to create the
file, then compilation proceeds anyways.  Thus, the absence of such a file does not indicate that there are no program elements with
infinite bounds.

Another limitation is that sub-object bounds narrowing is not currently implemented.

This version of DataShield has only been tested on C source code, not C++.

Furthermore, it has only been tested with MPX-based coarse-grained bounds checks, and the revised
section layout may be incompatible with the other coarse-grained bounds checking methods, since the
boundary between the unsafe and safe regions was shifted down to enable placing the text section
above that boundary.  Other coarse-grained bounds checking methods may assume that the boundary is
at 4GiB.  It may be possible to revert back to the original boundary or to update the other
bounds-checking methods to re-enable them.

# License

For LLVM see: compiler/llvm/LICENSE.txt

For our code: this code is dual-licensed under the LLVM license (University of Illinois/NCSA Open Source License) and the GPLv3. 
Choose whatever fits your requirements.

# Building the Compiler

## 0. Setup and environment

We built DataShield in 2016, so baseline your software around that date. You can reproduce our experiments when using

* Ubuntu 16.04 LTS
* `clang-3.9`, `ninja-build`, `cmake`, `binutils-dev` (of Ubuntu 16.04)
* If you have more than one clang installed, either adapt the makefiles or use `update-alternatives clang` to set `clang` to `clang-3.9` and `clang++` to `clang++-3.9`

## 1. Clone this repo

Clone this repo into `$HOME/research`

There are a lot of build scripts to glue everything together and make the
build reproducible.  The scripts all assume you cloned in the above directory.

    cd ~
    mkdir research
    cd research
    git clone git@github.com:HexHive/datashield.git

## 2. Build the compiler

The compiler is built using the normal LLVM build process.  Consult the LLVM
documentation if you have trouble.

You may use my build scripts or change the options if you know what you're
doing.

DataShield has 3 different configurations:

1. debug - debug info, unoptimized, with instrumentation
2. baseline - optimized, no instrumentation
3. release - optimized, with instrumentation

If you just want to experiment with DataShield debug might be the best:

    cd ~/research/datashield/compiler
    mkdir build-debug
    cd build-debug
    ../lto_cmake_debug.sh
    ninja install

You can build release if you care about compile times:

    cd ~/research/datashield/compiler
    mkdir build-release
    cd build-release
    ../lto_cmake_release.sh
    ninja install

Baseline is the same as release for the compiler since the compiler itself is
not instrumented, but you need to build it if you want a baseline comparison for benchmarking:

    cd ~/research/datashield/compiler
    mkdir build-baseline
    cd build-baseline
    ../lto_cmake_baseline.sh
    ninja install

## 4. Build libc


To build a configuration, just run `build-$configuration`

    cd $HOME/research/datashield/libc
    ./build-debug.py
    ./build-baseline.py
    ./build-release.py

The scripts create a different install directory for each configuration, so you
don't have to rebuild every time you want to test a different configuration.
They are:

    $HOME/research/datashield/ds_sysroot_debug
    $HOME/research/datashield/ds_sysroot_baseline
    $HOME/research/datashield/ds_sysroot_release

## 4. Build libcxx

Building libcxx is basically the same as building libc.  It has the same three configurations.  Running `build.py <config>` builds everything.  Otherwise run `build.py` with no arguments for a help message.

    cd $HOME/research/datashield/libcxx
    ./build.py <config>

# Compiling Instrumented Programs

You need a lot of options to be able to build with our custom libc, libcxx, and
various protections.  There are scripts in `$HOME/research/datashield/bin` that make this much easier.
The scripts directory (`$HOME/research/datashield/bin`) needs to be in your `PATH` for the scripts to work.

## Build Hello World

First, you should build a "Hello World" program to make sure your build is sane.

     cd $HOME/research/datashield/test/hand-written/hello_world
     make
     ./test

It should print out a whole bunch of log information and "hello world" and
"good bye."  If not, something is seriously wrong and you should create a
GitHub issue.

If you look at
`$HOME/research/datashield/test/hand-written/hello_world/Makefile` you will see
that there are multiple options for the variable `CC`.  To chose which set of
options you want you just change `CC` to one of the scripts in
`$HOME/research/datashield/bin`.  They all start with `musl-clang-*`.

## Build Hello World in C++

Building C++ (versus C) is basically the same but you need to use the C++ scripts.
It's a good idea to build "Hello World" in C an C++ as a sanity check.

     cd $HOME/research/tests/hand-written/hello_worldxx
     make
     ./test

## Compiler options

You should use the scripts, but if you need/want to change something and know
what you're doing you can give options manually.  These are the options that the scripts setup:

* `-datashield-lto` enables the datashield pass (required)
* `-T../../linker/linker_script.lds` this passes our script to the linker (required)
* `-datashield-debug-mode` prints debug logs at runtime
* `-datashield-save-module-after` saves the compiled module to a file after datashield's transformation
* `-datashield-save-module-before` saves the compiled module to a file before datashield's transformation
* `-debug-only=datashield` prints debug logs at compile time

The following are mutually exclusive:
* `-datashield-use-mask` use the software mask coarse bounds check options
* `-datashield-use-prefix-check` give this option if you want prefix or MPX

The following are mutually exclusive:
* `-datashield-use-prefix` give this option if you want prefix or MPX
* `-datashield-use-late-mpx` give this option if you want prefix or MPX

Must be used with `-datashield-use-mask`
* `-datashield-intergity-only-mode` only protect stores
* `-datashield-confidentiality-only-mode` only protect loads
* `-datashield-separation-mode` basic arithmetic does not propagate sensitivity

Two options for compiling system libraries:
* `-datashield-library-mode` for compiling libraries with sandboxing only
* `-datashield-modular` run the pass without LTO

## Intel Labs experimental extensions

This version of DataShield contains a number of extensions beyond the original version
of DataShield described in the AsiaCCS 2017 paper.  Here is a partial list:

* Various bug fixes
* Support for:
  * programs with more complex code constructs than were previously supported.
  * secrets embedded as immediate operands.
  * linking code above data so that it is effectively treated as execute-only due to bounds checking.
  * protecting sensitive spilled register data.
  * zeroing sensitive memory allocations.
  * an injector tool to inject secrets into binaries that contain empty placeholders for secrets.
  * eliding redundant bounds checks.
  * redefining infinite bounds to extend down to zero rather than just down to the boundary between the unsafe and safe regions.
  * conservatively treating function pointer invocations as having all sensitive arguments and return values.
  * a sensitivity propagation algorithm that follows edges in the AST rather than iterating through instructions.  The logic may be easier to follow and the code is shorter, although the runtime is sometimes longer.

We describe some of these extensions below:

### Execute-only code

The linker script was revised to place the text section above the sensitive data
so that it effectively becomes execute-only, except with respect to constant
pools and certain uninstrumented accesses (e.g. inline assembly, uninstrumented
libraries, etc.).  Accesses to sensitive data are subject to fine-grained bounds
checks, so that should prevent accesses to the text section unless infinite
bounds were associated with the data.  Unsafe accesses are subject to coarse-
grained bounds checks, which should also prevent access to the text section.
However, placing code above the 4GiB boundary resulted in code addresses too
large to fit in 32 bits, which generated linkage issues.  Thus, we lowered the
boundary between unsafe and safe data so that the text section in our test cases
stayed within the 4GiB boundary.

### Yolk data

The term "yolk data" refers to data that is attached to a specific set of program
statements such that only those statements should have access to the data.  Yolk
data is identified in programs using the annotation "__attribute__((annotate("yolk")))"
The compiler enforces that by embedding the data into the text section as
immediate operands for just the instructions that directly reference the data in
the program.  The execute-only protections for the code help to prevent the
yolk data from leaking.  However, we assume that some control-flow integrity
enforcement mechanism would also be used to defend against possible attacks based on
control-flow manipulation that could leak the yolk data.

### Yolk data injector tool

There are mechanisms such as Kubernetes Secrets for distributing container images
separately from the secrets that are used within those images.  The secrets are
distributed as files, so a mechanism is needed to weave those secrets into the
appropriate portions of the program code prior to the program executing.

The compiler and linker have been enhanced to emit information about the location of
immediate operands that are designated to contain yolk data.  An injector tool to
weave yolk data into a binary is in tests/hand-written/injector along with a
sample application and yolk data file.

### SafeStack integration

SafeStack is an LLVM extension that moves each stack allocation to an unsafe stack if
the compiler is unable to verify that all accesses to that allocation are safe, i.e.
within bounds.  SafeStack can be usefully combined with DataShield to protect
sensitive spilled register contents and to potentially reduce the overhead of
protecting sensitive stack allocations that can be stored on the safe stack, since
the compiler statically verifies that no bounds checks are needed for accesses to
those allocations.

This extension is based on patches that were previously submitted to the LLVM project.
See this discussion for details: http://lists.llvm.org/pipermail/llvm-dev/2017-February/109933.html

That discussion also covers the approach for eliding redundant MPX bounds checks.

### Zeroing sensitive memory allocations

Sensitive memory allocated by the musl-based runtime library is zeroed prior to
being returned to the program to help prevent misuse of data that may have previously
been stored in that memory.

### Redefined infinite bounds

Infinite bounds are used for some sensitive objects when computing more precise
bounds is unsupported.  In the original version of DataShield, infinite bounds actually
only extended down to the base of the sensitive memory region.  This version of
DataShield extends the base to 0, with the intent of helping to enable the use of
pointers returned by external, uninstrumented functions (in the unsafe region).
However, it may be worthwhile to revisit this modification in the future.

### Function pointer handling

The DataShield pass does not currently have the ability to identify all possible function
pointers that may be passed to a particular function pointer invocation site.  Thus, it
conservatively assumes that all inputs to and return values from functions invoked indirectly
are sensitive.
