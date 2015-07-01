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

SUBCOMMAND_LIB = 'libccli'
SUBCOMMAND_PATH = '%s/%s' % (RUNPATH, SUBCOMMAND_LIB)
SUBCOMMAND_EXTS = ['.sh', '.py'] # valid scripts
SUBCOMMANDS = {} # populated in scan_subcommands()
SUBCOMMAND_DESCRIPTIONS = {
    'init':        "Initialise Chef environment",
    'vm':          "Manage chef virtual machines",
    'build':       "Build Chef in a given configuration",
    'run':         "Run Chef in a given mode",
    'smtlibdump':  "Dump collected queries in SMT-LIB format",
    'compare':     "Compare two query dumps on logic equality",
    'smtlib-sort': "Sort query dumps by 'interesting'",
    'docker':      "Run docker container (useful for debugging/tinkering)",
    'help':        "Display this help",
}
SUBCOMMAND_IGNORED = ['__init__.py', 'libccli.py', 'utils.sh']


# EXECUTION ====================================================================

def scan_subcommands():
    for f in os.listdir(SUBCOMMAND_PATH):
        name, ext = os.path.splitext(os.path.basename(f))
        if os.path.isdir(f) \
        or ext not in SUBCOMMAND_EXTS \
        or (name + ext) in SUBCOMMAND_IGNORED:
            continue
        description = SUBCOMMAND_DESCRIPTIONS.get(name, '')
        SUBCOMMANDS[name] = {
            'command': '%s/%s' % (SUBCOMMAND_PATH, f),
            'description': description
        }


def run_subcommand(name):
    subcommand = SUBCOMMANDS.get(name, None)

    if subcommand is None:
        die_help("Unknown command: `%s`" % name)
    if subcommand == 'help':
        help()

    env = os.environ
    env['INVOKENAME'] = '%s %s' % (INVOKENAME, name)
    os.execve(SUBCOMMANDS[name]['command'], sys.argv[1:], env)


# MAIN =========================================================================

def usage(file=sys.stdout):
    print("Usage: %s COMMAND [ARGUMENTS ...]" % INVOKENAME, file=file)


def help():
    print("%s: Command line interface to chef\n" % INVOKENAME)
    usage()
    print("\nCommands:")
    for name in sorted(SUBCOMMANDS) + {'help':SUBCOMMAND_DESCRIPTIONS['help']}:
        subcmd = SUBCOMMANDS[name]
        print('  {:<12} %s'.format(name) % subcmd['description'])
    exit(1)


def die_help(msg: str):
    print(msg, file=sys.stderr)
    usage(file=sys.stderr)
    print("Run `%s help` for help." % INVOKENAME, file=sys.stderr)
    exit(1)


if __name__ == '__main__':
    # get subcommand from command line:
    if len(sys.argv) < 2:
        die_help("missing command")
    subcommand = sys.argv[1]

    # fix help:
    if subcommand in ['-h', '--help']:
        subcommand = 'help'

    # handle subcommand:
    scan_subcommands()
    run_subcommand(subcommand)
