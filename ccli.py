#!/usr/bin/env python3
#
# ccli - Chef Command Line Interface
# Runs scripts that are in libccli

import sys
import os
import libccli.libccli


RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
INVOKENAME = os.path.basename(RUNNAME)

COMMAND_LIB = 'libccli'
COMMAND_PATH = '%s/%s' % (RUNPATH, COMMAND_LIB)
COMMAND_EXTS = ['.sh', '.py'] # valid scripts
COMMANDS = {} # populated in scan_commands()
COMMAND_DESCRIPTIONS = {
    'init':          "Initialise Chef environment",
    'vm':            "Manage chef virtual machines",
    'build':         "Build Chef in a given configuration",
    'run':           "Run Chef in a given mode",
    'smtlib-dump':   "Dump collected queries in SMT-LIB format",
    'smtlib-compare':"Compare two query dumps on logic equality",
    'smtlib-sort':   "Sort query dumps by 'interesting'",
    'docker':        "Run docker container (useful for debugging/tinkering)",
    'help':          "Display this help",
}
COMMAND_IGNORED = ['__init__.py', 'libccli.py', 'utils.sh']


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
    COMMANDS['help'] = { 'description': COMMAND_DESCRIPTIONS.get('help', '') }


def run_command(name):
    if name == 'help':
        help()

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
    print("%s: Command line interface to chef\n" % INVOKENAME)
    usage()
    print("\nCommands:")
    for name in sorted(COMMANDS):
        cmd = COMMANDS[name]
        print('  {:<15} %s'.format(name) % cmd['description'])
    exit(1)


def die_help(msg: str):
    print(msg, file=sys.stderr)
    usage(file=sys.stderr)
    print("Run `%s help` for help." % INVOKENAME, file=sys.stderr)
    exit(1)


if __name__ == '__main__':
    # get command from command line:
    if len(sys.argv) < 2:
        die_help("missing command")
    command = sys.argv[1]

    # fix help:
    if command in ['-h', '--help']:
        command = 'help'

    # handle command:
    scan_commands()
    run_command(command)
