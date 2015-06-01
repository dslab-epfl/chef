#!/usr/bin/env python3

import argparse
import sys
import os
from libccli.vm import VM as VM
import libccli.libccli


global DATAROOT
RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
DATAROOT = '/var/lib/chef'


def command_init():
    if os.path.isdir(DATAROOT):
        print("Chef has already been initialised (%s already exists)"
              % DATAROOT, file=sys.stderr)
        exit(1)
    if os.geteuid() != 0:
        print("Please run `%s init` as root" % RUNNAME, file=sys.stderr)
        exit(1)
    try:
        print("Creating %s" % DATAROOT)
        os.mkdir(DATAROOT)
    except PermissionError:
        print("Permission denied", file=sys.stderr)
        exit(1)
    libccli.set_permissions(DATAROOT)


def command_vm():
    VM.main(sys.argv[1:])


def command_run():
    print("Not yet implemented")
    exit(1)


def command_build():
    os.execve('%s/libccli/build.sh' % RUNPATH,
              sys.argv[1:],
              {'INVOKENAME': '%s build' % sys.argv[0]})


def command_help():
    print("%s: Command line interface to chef\n" % sys.argv[0])
    print("Usage: %s COMMAND [ARGUMENTS ...]\n" % sys.argv[0])
    print("Commands:")
    print("  init      Initialise Chef environment in /var/lib/chef")
    print("  vm        Manage chef virtual machines")
    print("  build     Build Chef in a given configuration")
    print("  run       Run Chef in a given mode")
    exit(1)


if __name__ == '__main__':
    command = sys.argv[1] if len(sys.argv) > 1 else 'help'
    handlers = { 'init': command_init,
                 'vm': command_vm,
                 'run': command_run,
                 'build': command_build,
                 'help': command_help }
    handler = handlers.get(command, command_help)
    handler()
