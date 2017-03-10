#!/usr/bin/python
import os, shutil, sys
from subprocess import check_call

def build_libcxx():
    os.chdir(start_dir)
    shutil.rmtree(LIBCXX_DIR, True)
    os.mkdir(LIBCXX_DIR)
    os.chdir(LIBCXX_DIR)
    check_call(LIBCXX_CMAKE, shell=True)
    check_call("ninja", shell=True)
    check_call("ninja install", shell=True)

def build_libcxxabi():
    os.chdir(start_dir)
    shutil.rmtree(LIBCXXABI_DIR, True)
    os.mkdir(LIBCXXABI_DIR)
    os.chdir(LIBCXXABI_DIR)
    check_call(LIBCXXABI_CMAKE, shell=True)
    check_call("ninja", shell=True)
    check_call("ninja install", shell=True)

def build_libunwind():
    os.chdir(start_dir)
    shutil.rmtree(LIBUNWIND_DIR, True)
    os.mkdir(LIBUNWIND_DIR)
    os.chdir(LIBUNWIND_DIR)
    check_call(LIBUNWIND_CMAKE, shell=True)
    check_call("ninja", shell=True)
    check_call("ninja install", shell=True)

def print_usage():
    print "USAGE: build.py [debug|release|baseline] <unwind|abi|cxx>"

start_dir = os.getcwd()

if len(sys.argv) == 1 or len(sys.argv) > 3:
    print_usage()
    exit()

if len(sys.argv) > 1:
    LIBCXX_DIR = "build-libcxx-" + sys.argv[1]
    LIBCXXABI_DIR = "build-libcxxabi-" + sys.argv[1]
    LIBUNWIND_DIR = "build-libunwind-" + sys.argv[1]
    LIBCXX_CMAKE = "../libcxx_cmake_" + sys.argv[1] + ".sh"
    LIBCXXABI_CMAKE = "../libcxxabi_cmake_" + sys.argv[1] + ".sh"
    LIBUNWIND_CMAKE = "../libunwind_cmake_" + sys.argv[1] + ".sh"

if len(sys.argv) == 2:
    build_libunwind()
    build_libcxxabi()
    build_libcxx()

if len(sys.argv) == 3:
    if sys.argv[2] == "unwind":
        build_libunwind()
    if sys.argv[2] == "abi":
        build_libcxxabi()
    if sys.argv[2] == "cxx":
        build_libcxx()
    




