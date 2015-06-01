# TODO: wrap everything in docker
# http://docker-py.readthedocs.org/en/latest/api/
#------------------------------------------------------------------------------#
#from docker import Client as DockerClient
#
#docker_name = 'dslab/s2e-chef'
#docker_tag = 'v0.6'
#docker_image = '%s:%s' % (DOCKER_NAME, DOCKER_TAG)
#docker_socket = 'unix://var/run/docker.sock'
#docker_uid = 431
#
#c = DockerClient(base_url=DOCKER_SOCKET)
#try:
#    c.inspect_image(DOCKER_IMAGE)
#except docker.errors.APIError:
#    print("%s: docker image not found" % DOCKER_IMAGE, file=stderr)
#    exit(1)
#------------------------------------------------------------------------------#

# TODO: use ACL
# http://pylibacl.k1024.org/module.html
#------------------------------------------------------------------------------#
# ACL (TODO make it work):
#acl = ACL(text="u::rwx,g::rx,o::rx")
#acl.applyto(path, posix1e.ACL_TYPE_DEFAULT)
#acl = ACL(text="u:431:rwx,g::rx,o::rx")
#acl.applyto(path)
#acl.applyto(path, posix1e.ACL_TYPE_DEFAULT)
#------------------------------------------------------------------------------#


import os
import argparse
import posix1e
from posix1e import ACL,Entry
import sys
import psutil
from libccli import libccli


global DATAROOT


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s.raw' % (DATAROOT, name)


    def exists(self):
        return os.path.exists(self.path)


    def create(self, size: int, **kwargs: dict):
        if self.exists():
            print("Machine [%s] already exists" % self.name, file=sys.stderr)
            exit(1)
        if not os.path.isdir(DATAROOT):
            print("Chef has not been initialised; run `ccli.py init` first",
                  file=sys.stderr)
            exit(1)

        try:
            open(self.path, 'a').close()
        except PermissionError:
            print("Permission denied", file=sys.stderr)
            exit(1)

        try:
            libccli.execute(['qemu-img', 'create', self.path, '%d' % size])
        except libccli.ExecError as e:
            print(e, file=sys.stderr)
            exit(1)

        libccli.set_permissions(self.path)


    def install(self, iso_path: str, **kwargs: dict):
        if not self.exists():
            print("Machine [%s] does not exist" % self.name, file=sys.stderr)
            if libccli.prompt_yes_no("Create now?", True):
                size = libccli.prompt_int("VM size in MiB", 10240)
                self.create(size)
            else:
                print("Not installing %s, aborting" % iso_path, file=sys.stderr)
                exit(1)

        if not os.path.exists(iso_path):
            print("%s not found" % iso_path, file=sys.stderr)
            exit(1)

        qemu_cmd = ['qemu-system-%s' % VM.arch,
                    '-enable-kvm',
                    '-cpu', 'host',
                    '-smp', '%d' % VM.cores,
                    '-m', '%d' % VM.memory,
                    '-vga', 'std',
                    '-net', 'user',
                    '-monitor', 'tcp::1234,server,nowait',
                    '-drive', 'file=%s,if=virtio' % self.path,
                    '-drive', 'file=%s,media=cdrom,readonly' % iso_path,
                    '-boot', 'order=d']
        print("executing: `%s`" % ' '.join(qemu_cmd))
        libccli.execute(qemu_cmd)


    def delete(self, **kwargs: dict):
        if not self.exists():
            print("Machine [%s] does not exist" % self.name)
            exit(1)
        try:
            os.unlink(self.path)
        except PermissionError:
            print("Permission denied", file=sys.stderr)
            exit(1)


    @staticmethod
    def main(argv: [str]):
        p = argparse.ArgumentParser(description="Handle Virtual Machines")

        pcmd = p.add_subparsers(help="Action", dest="Action")
        pcmd.required = True

        # create
        pcreate = pcmd.add_parser('create', help="Create a new VM")
        pcreate.set_defaults(action=VM.create)
        pcreate.add_argument('name', help="Machine name")
        pcreate.add_argument('size', help="VM size (in MB)", type=int)

        # install
        pinstall = pcmd.add_parser('install', help="Install an OS to a VM")
        pinstall.set_defaults(action=VM.install)
        pinstall.add_argument('name', help="Machine name")
        pinstall.add_argument('iso_path', help="Path to ISO file containing the OS")

        # delete
        pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
        pdelete.set_defaults(action=VM.delete)
        pdelete.add_argument('name', help="Machine name")

        args = p.parse_args(argv[1:])
        kwargs = vars(args) # make it a dictionary, for easier use

        vm = VM(args['name'])
        args['action'](vm, **kwargs)
