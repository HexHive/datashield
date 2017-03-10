#!/usr/bin/python
from base import get_clang_command
from subprocess import check_call
import sys

args = sys.argv[1:]
args.append("-O3")
ds_linker_args = ["-Wl",\
"-plugin-opt=-datashield-lto",\
"-plugin-opt=-datashield-use-mask",\
"-plugin-opt=-datashield-save-module-after",\
"-plugin-opt=-datashield-debug-mode",\
"-plugin-opt=-debug-only=datashield",\
"-T../../linker/linker_script.lds"]
cmd = get_clang_command("release", args, ds_linker_args, cxx=True)
print cmd
check_call(cmd, shell=True)




