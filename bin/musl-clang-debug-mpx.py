#!/usr/bin/python
from base import get_clang_command
from subprocess import check_call
import sys

args = sys.argv[1:]
ds_linker_args = ["-Wl",\
                  "-mllvm,-datashield-lto",\
                  "-mllvm,-datashield-use-mpx",\
                  "-mllvm,-datashield-save-module-before",\
                  "-mllvm,-datashield-save-module-after",\
                  "-mllvm,-datashield-debug-mode",\
                  "-T$HOME/research/datashield/linker/linker_script.lds"]
cmd = get_clang_command("debug", args, ds_linker_args, cxx=False).replace('"', '\\"')
print cmd
check_call(cmd, shell=True)




#                  "-mllvm,-datashield-save-module-before",\
#                  "-mllvm,-datashield-save-module-after",\
#                  "-mllvm,-debug-only=datashield",\
# "-plugin-opt=-debug-pass=Structure",\
