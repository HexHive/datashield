# Datashield

# License

For LLVM see: compiler/llvm/LICENSE.txt

For our code: GPLv3

# Building the Compiler

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
    cd build
    ../lto_cmake_debug.sh
    ninja
    ninja install

You can build release if you care about compile times:

    cd ~/research/datashield/compiler
    mkdir build-release
    cd build
    ../lto_cmake_release.sh
    ninja
    ninja install

Baseline is the same as release for the compiler since the compiler itself is
not instrumented, but you need to build it if you want a baseline comparison for benchmarking:

    cd ~/research/datashield/compiler
    mkdir build-baseline
    cd build
    ../lto_cmake_baseline.sh
    ninja
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

Building libcxx is basically the same as building libc.  It has the same three configurations.  Running `build.py all` builds everything.  Otherwise run `build.py` with no arguments for a help message.

    cd $HOME/research/datashield/libc
    ./build.py all

# Compiling Instrumented Programs

You need a lot of options to be able to build with our custom libc, libcxx, and
various protections.  There are scripts in `$HOME/research/bin` that make this much easier.

## Build Hello World

First, you should build a "Hello World" program to make sure your build is sane.

     cd $HOME/research/tests/hand-written/hello_world
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
