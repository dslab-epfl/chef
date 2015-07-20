#!/usr/bin/env python3

# Initialise the Chef environment on the system.

import sys
import os
import utils

RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
INVOKENAME = os.environ.get('INVOKENAME', os.path.basename(RUNNAME))
DATAROOT = os.environ.get('CHEF_DATAROOT', '/var/local/chef')
DATAROOT_TREE = [
    DATAROOT,
    DATAROOT+'/expdata',
    DATAROOT+'/vm',
]

class Init:
    @staticmethod
    def usage(file=sys.stdout):
        print("Usage: %s [OPTION]" % INVOKENAME, file=file)


    @staticmethod
    def help():
        print("%s: Initialise the Chef environment\n" % INVOKENAME)
        Init.usage()
        print("\nOptions:")
        print("  -h, --help  Display this help")
        print("\nThe Chef environment is initialised in %s. " % (DATAROOT) +
              "Set the CHEF_DATAROOT environment variable to change the path.")
        exit(1)


    @staticmethod
    def die_help(msg: str):
        print(msg, file=sys.stderr)
        Init.usage(file=sys.stderr)
        print("Run `%s help` for help." % INVOKENAME, file=sys.stderr)
        exit(1)


    @staticmethod
    def main(args: [str]):
        if len(args) > 1:
            if args[1] in ['-h', '--help']:
                Init.help()
            else:
                Init.die_help("Unknown argument: %s" % args[1])

        if os.geteuid() != 0:
            print("Please run `%s init` as root" % RUNNAME, file=sys.stderr)
            exit(1)
        try:
            for directory in DATAROOT_TREE:
                if os.path.isdir(directory):
                    print('%s [skip]' % directory)
                    continue
                else:
                    print('%s' % directory)
                os.mkdir(directory)
                utils.set_permissions(directory)
        except PermissionError:
            print("Permission denied", file=sys.stderr)
            exit(1)


if __name__ == '__main__':
    Init.main(sys.argv)
