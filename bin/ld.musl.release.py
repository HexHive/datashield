#!/usr/bin/python
from subprocess import check_call
import sys
from base import get_ld_command

cmd = get_ld_command("release", cxx=False, cmdLineArgs=sys.argv[1:])
print cmd
check_call(cmd, shell=True)
