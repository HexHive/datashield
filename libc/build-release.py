#!/usr/bin/python
import os, shutil
from subprocess import check_call

BUILD_DIR = "build-release"

shutil.rmtree(BUILD_DIR, True)
os.mkdir(BUILD_DIR)
os.chdir(BUILD_DIR)
check_call("../release-configure.sh", shell=True)
check_call("make", shell=True)
check_call("make install", shell=True)
