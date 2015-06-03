#!/usr/bin/env python3

import argparse
import sys
import os
import libccli.libccli


global DATAROOT
RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
INVOKENAME = os.path.basename(RUNNAME)
DATAROOT = '/var/lib/chef'


def shell_command(name: str):
    os.execve('%s/libccli/%s.sh' % (RUNPATH, name),
              sys.argv[1:],
              {'INVOKENAME': '%s %s' % (INVOKENAME, name)})


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
    from libccli.vm import VM as VM
    VM.main(sys.argv[1:])


def command_run():
    print("Not yet implemented")
    exit(1)


def command_smtlibdump():
    shell_command('smtlibdump')


def command_docker():
    shell_command('docker')


def command_build():
    shell_command('build')


def command_help():
    print("%s: Command line interface to chef\n" % INVOKENAME)
    print("Usage: %s COMMAND [ARGUMENTS ...]\n" % INVOKENAME)
    print("Commands:")
    print("  init        Initialise Chef environment in %s" % DATAROOT)
    print("  vm          Manage chef virtual machines")
    print("  build       Build Chef in a given configuration")
    print("  run         Run Chef in a given mode")
    print("  smtlibdump  Dump collected queries in SMT-Lib format")
    print("  docker      Run docker container (useful for debugging/tinkering)")
    exit(1)


if __name__ == '__main__':
    command = sys.argv[1] if len(sys.argv) > 1 else 'help'
    handlers = { 'init': command_init,
                 'vm': command_vm,
                 'run': command_run,
                 'build': command_build,
                 'smtlibdump': command_smtlibdump,
                 'docker': command_docker,
                 'help': command_help }
    handler = handlers.get(command, command_help)
    handler()
