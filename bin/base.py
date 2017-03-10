#!/usr/bin/python
import os, sys
from subprocess import check_call

def get_clang_command(build_type, cmdLineArgs, ld, cxx, linker_args):
    DS_HOME=os.path.join(os.getenv("HOME"), "research", "datashield")
    DS_SYSROOT=os.path.join(DS_HOME, "ds_sysroot_{0}".format(build_type))
    DS_LIB_DIR=os.path.join(DS_SYSROOT, "lib")
    CC=os.path.join(DS_SYSROOT, "bin", "clang")
    CXX=os.path.join(DS_SYSROOT, "bin", "clang")

    if cmdLineArgs.count("-c") > 0:
        linking = False
    else:
        linking = True

    if cxx:
      args = [CXX]
    else:
      args = [CC]
    args.append("-static")
    args.append("-v")
    args.append("-flto")
    args.append("-nostdlib")
    args.append("-nostdinc")
    args.append("-isysroot")
    args.append(DS_SYSROOT)
    args.append("--sysroot")
    args.append(DS_SYSROOT)
    if cxx:
      args.append("-isystem")
      args.append(os.path.join(DS_SYSROOT, "include", "c++", "v1"))
    args.append("-isystem")
    args.append(os.path.join(DS_SYSROOT, "lib", "clang", "3.9.0", "include"))
    args.append("-isystem")
    args.append(os.path.join(DS_SYSROOT, "include"))
    if linking:
        args.append("-fuse-ld={0}".format(ld))
	args.append(",".join(linker_args))
    args.extend(cmdLineArgs)
    cmd = " ".join(args)
    return cmd
 
def get_ld_command(build_type, cxx=False, cmdLineArgs=None):
  DS_HOME=os.path.join(os.getenv("HOME"),"research", "datashield")
  DS_SYSROOT=os.path.join(DS_HOME, "ds_sysroot_{0}".format(build_type))
  DS_LIB_DIR=os.path.join(DS_SYSROOT, "lib")
  CC=os.path.join(DS_SYSROOT, "bin", "clang")
  CXX=os.path.join(DS_SYSROOT, "bin", "clang")
  LD="ld"

  if cmdLineArgs.count("-E"):
    cmdLineArgs.remove("-E")
  args = [LD]
  if cxx:
      args.append("--eh-frame-hdr")
  args.extend(cmdLineArgs)
  args.append("-L"+DS_LIB_DIR)
  args.append(os.path.join(DS_LIB_DIR, "crt1.o"))
  args.append(os.path.join(DS_LIB_DIR, "crti.o"))
  args.append(os.path.join(DS_LIB_DIR, "crtn.o"))
  if cxx:
    args.append(os.path.join(DS_LIB_DIR, "libc++.a"))
    args.append(os.path.join(DS_LIB_DIR, "libc++abi.a"))
    args.append(os.path.join(DS_LIB_DIR, "libunwind.a"))
  args.append(os.path.join(DS_LIB_DIR, "libc.a"))
  #args.append(os.path.join(DS_LIB_DIR, "libm.a"))
  cmd = " ".join(args)
  return cmd

'''
def get_index_of_last_plugin_opt(cmdLineArgs):
  index = -1
  for i,e in enumerate(cmdLineArgs):
    if e.startswith("-plugin-opt="):
       index = i
    if i == -1:
	raise "there were no plugin opts?!?!"
  return i
'''

