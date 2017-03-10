#!/usr/bin/python
from base import get_clang_command
from subprocess import check_call
import sys

args = sys.argv[1:]
ds_linker_args = ["-Wl",\
"-plugin-opt=-datashield-lto",\
"-plugin-opt=-datashield-use-mask",\
"-plugin-opt=-datashield-save-module-after",\
"-plugin-opt=-datashield-debug-mode",\
"-plugin-opt=-debug-only=datashield",\
"-T../../linker/linker_script.lds"]
#args.append(ds_linker_args)
cmd = get_clang_command("debug", args, "musl.debug.py", False, ds_linker_args)
print cmd
check_call(cmd, shell=True)




