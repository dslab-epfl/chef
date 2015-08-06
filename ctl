#!/usr/bin/env python3

# ctl - Command line tools for Chef
# This script is a common interface for running the scripts located in ctltools/
# that make up the base functionalities. Each of the script is ran as a
# subcommand to ctl (similar to command line utilities like git, ip,
# docker, ...).
#
# Maintainers:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)


import sys
import os
from collections import OrderedDict


RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
INVOKENAME = os.path.basename(RUNNAME)

COMMAND_LIB = 'ctltools'
COMMAND_PATH = '%s/%s' % (RUNPATH, COMMAND_LIB)
COMMAND_EXTS = ['.sh', '.py'] # valid scripts
COMMANDS = {} # populated in scan_commands()
COMMAND_DESCRIPTIONS = {
    'build':         "Build Chef in a given configuration",
    'run':           "Run Chef in a given mode",
    'vm':            "Manage chef virtual machines",
    'smtlib-dump':   "Dump collected queries in SMT-LIB format",
    'smtlib-compare':"Compare two query dumps on logic equality",
    'smtlib-sort':   "Sort query dumps by 'interesting'",
}
COMMAND_IGNORED = ['utils.py', 'utils.sh']


# EXECUTION ====================================================================

def scan_commands():
    for f in os.listdir(COMMAND_PATH):
        name, ext = os.path.splitext(f)
        if os.path.isdir(f) \
        or ext not in COMMAND_EXTS \
        or (name + ext) in COMMAND_IGNORED:
            continue
        description = COMMAND_DESCRIPTIONS.get(name, '')
        COMMANDS[name] = {
            'command': '%s/%s' % (COMMAND_PATH, f),
            'description': description
        }


def run_command(name):
    command = COMMANDS.get(name, None)
    if command is None:
        die_help("Unknown command: `%s`" % name)

    env = os.environ
    env['INVOKENAME'] = '%s %s' % (INVOKENAME, name)
    os.execve(COMMANDS[name]['command'], sys.argv[1:], env)


# MAIN =========================================================================

def usage(file=sys.stdout):
    print("Usage: %s COMMAND [ARGUMENTS ...]" % INVOKENAME, file=file)


def help():
    usage()
    print("\n%s: Command Line Tools for Chef\n" % INVOKENAME)
    print("Commands:")
    for c in ['build', 'run', 'vm',
              'smtlib-dump', 'smtlib-compare', 'smtlib-sort', 'env']:
        print("  {:<15}: {}".format(c, COMMANDS[c]['description']))
    print("\nMost commands can be run with `-h` for more information")
    exit(1)


def die_help(msg: str):
    print(msg, file=sys.stderr)
    usage(file=sys.stderr)
    print("Run `%s help` for help." % INVOKENAME, file=sys.stderr)
    exit(1)


if __name__ == '__main__':
    # get command from command line:
    if len(sys.argv) < 2:
        die_help("Missing command")
    command = sys.argv[1]

    # get legal commands:
    scan_commands()

    # command, or help:
    if command.lower() in ['-h', '--help', '-help', 'help', 'halp']:
        if len(sys.argv) > 2:
            die_help("Trailing arguments: %s" % ' '.join(sys.argv[2:]))
        help()
    else:
        run_command(command)
