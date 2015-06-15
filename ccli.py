#!/usr/bin/env python3
#
# ccli - Chef Command Line Interface

import argparse
import sys
import os
import libccli.libccli


global DATAROOT
RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
INVOKENAME = os.path.basename(RUNNAME)
DATAROOT = '/var/lib/chef'


# HANDLERS =====================================================================

def external_command(name: str, type: str):
    env = os.environ
    env['INVOKENAME'] = '%s %s' % (INVOKENAME, name)
    os.execve('%s/libccli/%s.%s' % (RUNPATH, name, type), sys.argv[1:], env)


def python_command(name: str):
    external_command(name, 'py')


def shell_command(name: str):
    external_command(name, 'sh')


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


# MAIN =========================================================================

def usage(file=sys.stdout):
    print("Usage: %s COMMAND [ARGUMENTS ...]" % INVOKENAME, file=file)


def help():
    print("%s: Command line interface to chef\n" % INVOKENAME)
    usage()
    print("\nCommands:")
    print("  init        Initialise Chef environment in %s" % DATAROOT)
    print("  vm          Manage chef virtual machines")
    print("  build       Build Chef in a given configuration")
    print("  run         Run Chef in a given mode")
    print("  smtlibdump  Dump collected queries in SMT-Lib format")
    print("  compare     Compare two query dumps on logic equality")
    print("  smtlib-sort Sort query dumps by 'interesting'")
    print("  docker      Run docker container (useful for debugging/tinkering)")
    exit(1)


def die_help(msg: str):
    print(msg, file=sys.stderr)
    usage(file=sys.stderr)
    print("Run `%s help` for help." % INVOKENAME, file=sys.stderr)
    exit(1)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        die_help("missing command")
    else:
        command = sys.argv[1]

    handlers = {'init'       : {'type':'MODULE',  'function':command_init},
                'vm'         : {'type':'MODULE',  'function':command_vm  },
                'run'        : {'type':'PYTHON'                          },
                'build'      : {'type':'SHELL'                           },
                'smtlibdump' : {'type':'SHELL'                           },
                'docker'     : {'type':'SHELL'                           },
                'compare'    : {'type':'SHELL'                           },
                'smtlib-sort': {'type':'SHELL'                           },
                'help'       : {'type':'BUILTIN', 'function':help        }}
    handler = handlers.get(command, {'type':'INVALID'})

    if handler['type'] == 'SHELL':
        shell_command(command)
    elif handler['type'] == 'PYTHON':
        python_command(command)
    elif handler['type'] == 'INVALID':
        die_help("Unknown command: %s" % command)
    else:
        handler['function']()
