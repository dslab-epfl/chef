#!/usr/bin/env python3
#
# ccli - Chef Command Line Interface
# Runs scripts that are in libccli

import sys
import os
from collections import OrderedDict


RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
INVOKENAME = os.path.basename(RUNNAME)

COMMAND_LIB = 'libccli'
COMMAND_PATH = '%s/%s' % (RUNPATH, COMMAND_LIB)
COMMAND_EXTS = ['.sh', '.py'] # valid scripts
COMMANDS = {} # populated in scan_commands()
COMMAND_DESCRIPTIONS = {
    'build':         "Build Chef in a given configuration",
    'run':           "Run Chef in a given mode",
    'clean':         "Clean the build results",
    'smtlib-dump':   "Dump collected queries in SMT-LIB format",
    'smtlib-compare':"Compare two query dumps on logic equality",
    'smtlib-sort':   "Sort query dumps by 'interesting'",
    'init':          "Initialise Chef environment",
    'vm':            "Manage chef virtual machines",
    'docker':        "Run docker container (useful for debugging/tinkering)",
    'env':           "List Chef-specific environment variables",
}
COMMAND_GROUPS = OrderedDict([
    ('Chef', ['build', 'run', 'clean']),
    ('SMTLIB', ['smtlib-dump', 'smtlib-compare', 'smtlib-sort']),
    ('Environment', ['init', 'vm', 'env']),
    ('Miscellaneous', []),
])
COMMAND_IGNORED = ['__init__.py', 'utils.py', 'utils.sh']


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


def help_group(group: str, used: [str]):
    print('  %s:' % group)
    for name in COMMANDS:
        if name in used:
            continue
        if name not in COMMAND_GROUPS[group] and group != 'Miscellaneous':
            continue
        print('    {:<15} %s'.format(name) % COMMANDS[name]['description'])
        used.append(name)


def help():
    print("%s: Command line interface to chef\n" % INVOKENAME)
    usage()
    print("\nCommands:")
    used = []
    for group in COMMAND_GROUPS:
        help_group(group, used)
    print("\nEach command can be run with `-h` for more information")
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
