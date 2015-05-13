#!/usr/bin/env python3

import argparse
import sys
import os
from ccli import vm
from ccli import libccli


global DATAROOT

RUNPATH = os.path.dirname(os.path.realpath(__file__))
RUNNAME = sys.argv[0]
DATAROOT = '/var/lib/chef'


def command_init(**kwargs: dict):
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


def command_vm(**kwargs: dict):
    v = vm.VM(kwargs['name'])
    kwargs['action'](v, **kwargs)


def command_run(**kwargs: dict):
    pass


def command_build(**kwargs: dict):
    kwargs['REMAINDER'].insert(0, '%s/build.sh' % RUNPATH)
    libccli.execute(kwargs['REMAINDER'])


def add_parser(p: argparse.ArgumentParser):
    pcmd = p.add_subparsers(help="Command", dest="Command")
    pcmd.required = True

    # init
    pinit = pcmd.add_parser('init', help="Initialise chef data directory (root only)")
    pinit.set_defaults(command=command_init)

    # vm
    pvm = pcmd.add_parser('vm', help="Handle virtual machines")
    pvm.set_defaults(command=command_vm)
    vm.VM.add_parser(pvm)

    # run
    prun = pcmd.add_parser('run', help="Run chef inside docker")
    prun.set_defaults(command=command_run)
    prun.add_argument('REMAINDER', nargs=argparse.REMAINDER)

    # build
    pbuild = pcmd.add_parser('build', help="Build chef")
    pbuild.set_defaults(command=command_build)
    pbuild.add_argument('REMAINDER', nargs=argparse.REMAINDER)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description="Command line interface to chef")
    add_parser(p)
    args = p.parse_args()
    kwargs = vars(args)
    kwargs['command'](**kwargs)
