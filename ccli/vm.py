#!/usr/bin/env python3

import os
import argparse
import docker
from docker import Client as DockerClient
from sys import stderr

DOCKER_NAME = 'dslab/s2e-chef'
DOCKER_TAG = 'v0.6'
DOCKER_IMAGE = '%s:%s' % (DOCKER_NAME, DOCKER_TAG)
DOCKER_SOCKET = 'unix://var/run/docker.sock'

# Default values:
VM_ARCH = 'x86_64'


def create(vm_name: str, vm_size: int, **kwargs: dict):
    vm_path = '/var/lib/chef/%s.raw' % vm_name
    print('creating %s ...' % vm_path)
    #os.subcommand(['qemu-img', 'create', vm_path, vm_size])


def install(vm_name: str, vm_iso: str, **kwargs: dict):
    c = DockerClient(base_url=DOCKER_SOCKET)
    try:
        c.inspect_image(DOCKER_IMAGE)
        print("Installing %s on %s" % (vm_iso, vm_name))
    except docker.errors.APIError:
        print("%s:%s: docker image not found" % DOCKER_IMAGE, file=stderr)
        exit(1)


def delete(vm_name: str, **kwargs: dict):
    pass


def parse_cli():
    p = argparse.ArgumentParser(description="Handle virtual machines")
    pcmd = p.add_subparsers(help="Subcommand", dest="Subcommand")
    pcmd.required = True

    # create
    pcreate = pcmd.add_parser('create', help="Create a new VM")
    pcreate.set_defaults(command=create)
    pcreate.add_argument('vm_name', help="Machine name")
    pcreate.add_argument('vm_size', help="VM size (in MB)", type=int)

    # delete
    pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
    pdelete.set_defaults(command=delete)
    pdelete.add_argument('vm_name', help="Machine name")

    args = p.parse_args()
    return vars(args)


def main():
    args = parse_cli()
    args['command'](**args)


if __name__ == '__main__':
    main()
