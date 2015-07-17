#!/usr/bin/env python3

import os
import argparse
import sys
import psutil
import utils


DATAROOT = os.environ.get('CHEF_DATAROOT', '/var/lib/chef')


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name):
        self.name = name
        self.path = '%s/%s.raw' % (DATAROOT, name)


    def exists(self):
        return self.name and os.path.exists(self.path)


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
            utils.execute(['qemu-img', 'create', self.path, '%d' % size])
        except utils.ExecError as e:
            print(e, file=sys.stderr)
            exit(1)

        utils.set_permissions(self.path)


    def install(self, iso_path: str, **kwargs: dict):
        if not self.exists():
            print("Machine [%s] does not exist" % self.name, file=sys.stderr)
            if utils.prompt_yes_no("Create now?", True):
                size = utils.prompt_int("VM size in MiB", 10240)
                self.create(size)
            else:
                print("Not installing %s, aborting" % iso_path, file=sys.stderr)
                exit(1)

        if not os.path.exists(iso_path):
            print("%s: ISO image not found" % iso_path, file=sys.stderr)
            exit(1)

        qemu_cmd = ['qemu-system-%s' % VM.arch,
                    '-enable-kvm',
                    '-cpu', 'host',
                    '-smp', '%d' % VM.cores,
                    '-m', '%d' % VM.memory,
                    '-vga', 'std',
                    '-net', 'user',
                    '-monitor', 'tcp::1234,server,nowait',
                    '-drive', 'file=%s,if=virtio,format=raw' % self.path,
                    '-drive', 'file=%s,media=cdrom,readonly' % iso_path,
                    '-boot', 'order=d']
        print("executing: `%s`" % ' '.join(qemu_cmd))
        utils.execute(qemu_cmd)


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
    def list(self, **kwargs: dict):
        for f in os.listdir(DATAROOT):
            bn, ext = os.path.splitext(f)
            if not ext == '.raw':
                continue
            print(bn)


    @staticmethod
    def main(argv: [str]):
        p = argparse.ArgumentParser(description="Handle Virtual Machines")

        pcmd = p.add_subparsers(help="Action", dest="action")
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

        # list
        plist = pcmd.add_parser('list', help="List existing VMs")
        plist.set_defaults(action=VM.list)

        args = p.parse_args(argv[1:])
        kwargs = vars(args) # make it a dictionary, for easier use

        if kwargs['action'] == plist:
            VM.list()
        else:
            vm = VM(kwargs.get('name', None))
            kwargs['action'](vm, **kwargs)


if __name__ == '__main__':
    VM.main(sys.argv)
