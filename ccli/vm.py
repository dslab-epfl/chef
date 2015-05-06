#!/usr/bin/env python3

# TODO: wrap everything in docker
#------------------------------------------------------------------------------#
#import docker
#
#c = DockerClient(base_url=DOCKER_SOCKET)
#try:
#    c.inspect_image(DOCKER_IMAGE)
#except docker.errors.APIError:
#    print("%s: docker image not found" % DOCKER_IMAGE, file=stderr)
#    exit(1)
#------------------------------------------------------------------------------#

import os
import argparse
import posix1e
from posix1e import ACL,Entry
from docker import Client as DockerClient
import sys
import grp


DOCKER_NAME = 'dslab/s2e-chef'
DOCKER_TAG = 'v0.6'
DOCKER_IMAGE = '%s:%s' % (DOCKER_NAME, DOCKER_TAG)
DOCKER_SOCKET = 'unix://var/run/docker.sock'
DOCKER_UID = 431

VM_ARCH = 'x86_64'
VM_DATA_BASE = '/var/lib/chef'
VM_CORES = 2 # TODO detect number of cores in /proc/cpuinfo
VM_MEMORY = 4096 # TODO calculate from host's main memory: 1/4, [2G, 4G]


class ExecError(Exception):
    def __init__(self, msg):
        self.message = msg
    def __str__(self):
        return self.message


def get_vm_path(vm_name: str):
    return '%s/%s.raw' % (VM_DATA_BASE, vm_name)


def execute(cmd: [str]):
    pid = os.fork()

    # child:
    if pid == 0:
        os.execvp(cmd[0], cmd)
        raise ExecError('exec(%s) failed' % ' '.join(cmd))

    # parent:
    (pid, status) = os.waitpid(pid, 0)
    return status >> 8


def set_permissions(path: str):
    try:
        os.chown(path, 0, grp.getgrnam('kvm').gr_gid)
        os.chmod(path, 0o775 if os.path.isdir(path) else 0o664)

        # ACL (TODO make it work):
        #acl = ACL(text="u::rwx,g::rx,o::rx")
        #acl.applyto(VM_DATA_BASE, posix1e.ACL_TYPE_DEFAULT)
        #acl = ACL(text="u:431:rwx,g::rx,o::rx")
        #acl.applyto(VM_DATA_BASE)
        #acl.applyto(VM_DATA_BASE, posix1e.ACL_TYPE_DEFAULT)

    except PermissionError:
        print("Cannot modify permissions for %s: Permission denied" % path,
              file=sys.stderr)
        exit(1)

def check_data_base():
    if os.path.isdir(VM_DATA_BASE):
        return
    try:
        print("Creating %s" % VM_DATA_BASE)
        os.mkdir(VM_DATA_BASE)
    except PermissionError:
        print("Could not create %s: Permission denied" % VM_DATA_BASE)
        exit(1)
    set_permissions(VM_DATA_BASE)


def create(vm_name: str, vm_size: int, **kwargs: dict):
    if os.geteuid() != 0:
        print("Please run `%s create` as root" % sys.argv[0], file=sys.stderr)
        exit(1)
    check_data_base()

    vm_path = get_vm_path(vm_name)
    if os.path.exists(vm_path):
        print("[%s] Cannot create machine: Already exists" % vm_name,
              file=sys.stderr)
        exit(1)

    try:
        open(vm_path, 'a').close()
    except PermissionError:
        print("[%s] Cannot create machine: Permission denied" % vm_name,
              file=sys.stderr)
        exit(1)

    try:
        execute(['qemu-img', 'create', vm_path, '%d' % vm_size])
    except ExecError as e:
        print(e, file=sys.stderr)
        exit(1)

    set_permissions(vm_path)


def install(vm_name: str, iso_path: str, **kwargs: dict):
    vm_path = get_vm_path(vm_name)
    if not os.path.exists(vm_path):
        print("[%s] Machine does not exist; run `%s create` first"
              % (vm_name, sys.argv[0]), file=sys.stderr)
        exit(1)

    execute(['qemu-system-%s' % VM_ARCH,
             '-enable-kvm',
             '-cpu', 'host',
             '-smp', '%d' % VM_CORES,
             '-m', '%d' % VM_MEMORY,
             '-vga', 'std',
             '-net', 'user',
             '-monitor', 'tcp::1234,server,nowait',
             '-drive', 'file=%s,if=virtio' % vm_path,
             '-drive', 'file=%s,media=cdrom,readonly' % iso_path,
             '-boot', 'order=d'])


def delete(vm_name: str, **kwargs: dict):
    vm_path = get_vm_path(vm_name)
    if not os.path.exists(vm_path):
        print("[%s] Machine does not exist" % vm_name)
        exit(1)

    try:
        os.unlink(vm_path)
    except PermissionError:
        print("[%s] Cannot delete machine: Permission denied" % vm_name,
              file=sys.stderr)
        exit(1)


def parse_cli():
    p = argparse.ArgumentParser(description="Handle virtual machines")
    pcmd = p.add_subparsers(help="Subcommand", dest="Subcommand")
    pcmd.required = True

    # create
    pcreate = pcmd.add_parser('create', help="Create a new VM  (root only)")
    pcreate.set_defaults(command=create)
    pcreate.add_argument('vm_name', help="Machine name")
    pcreate.add_argument('vm_size', help="VM size (in MB)", type=int)

    # install
    pinstall = pcmd.add_parser('install', help="Install an OS to a VM")
    pinstall.set_defaults(command=install)
    pinstall.add_argument('vm_name', help="Machine name")
    pinstall.add_argument('iso_path', help="Path to ISO file containing the OS")

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
