#!/usr/bin/env python3

import os
import argparse
import sys
import psutil
import utils
import tempfile
import subprocess


DATAROOT = os.environ.get('CHEF_DATAROOT', '/var/local/chef')
INVOKENAME = os.environ.get('INVOKENAME', sys.argv[0])
SRC_ROOT = os.path.dirname(os.path.dirname(__file__))
VMROOT = '%s/vm' % DATAROOT
INSTALLSCRIPTS_ROOT, _ = os.path.splitext(__file__)
PREPARED = ['Debian']


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s.raw' % (VMROOT, name)


    def exists(self):
        return self.name and os.path.exists(self.path)


    def prepare(self, size: int, force: bool):
        if self.exists():
            if force:
                print("[SKIP] image creation: Machine [%s] already exists" % self.name)
                return
            else:
                print("Machine [%s] already exists" % self.name, file=sys.stderr)
                exit(1)
        if not os.path.isdir(VMROOT):
            print("%s: directory not found (you may need to initialise Chef first)"
                  % VMROOT, file=sys.stderr)
            exit(1)
        try:
            open(self.path, 'a').close()
        except PermissionError:
            print("Permission denied", file=sys.stderr)
            exit(1)
        if utils.execute(['qemu-img', 'create', self.path, '%dM' % size],
                         msg="execute qemu-img") != 0:
            exit(1)
        utils.set_permissions(self.path)


    def partition(self):
        if utils.execute(['sfdisk', '-d', self.path]) == 0:
            print("[SKIP] partitioning: partition table already exists")
            return

        print("creating partition table ...")
        if utils.execute(['fdisk', self.path],
        #                             , create a new empty DOS partition table
        #                             |  , add a new partition
        #                             |  |  , partition type (primary)
        #                             |  |  |  , partition number
        #                             |  |  |  |  , first sector
        #                             |  |  |  |  | , last sector
        #                             |  |  |  |  | | , table to disk and exit
        #                             |  |  |  |  | | |
                         stdin='o\nn\np\n1\n\n\nw\n',
                         msg="create partition") != 0:
            exit(1)


    def format(self, fs:str='ext4'):
        # Get free loop device:
        out, _, retval = utils.execute(['losetup', '-f'], iowrap=True,
                                             msg="get free loop device")
        if retval != 0:
            exit(1)
        loopdev = out.strip()
        print("[INFO] using %s as loop device" % loopdev)

        # Connect loop device:
        try:
            if utils.sudo(['losetup', '--partscan', loopdev, self.path],
                          msg="attach to loop device %s" % loopdev) != 0:
                exit(1)
        except KeyboardInterrupt:
            exit(127)

        # Format:
        try:
            out,_,retval = utils.sudo(['blkid', '-o', 'value', '-s', 'TYPE',
                                      '%sp1' % loopdev], iowrap=True)
            if retval == 0 and out.strip() == fs:
                print('[SKIP] formatting: already formatted as %s' % fs)
            else:
                retval = utils.sudo(['mkfs.%s' % fs, '%sp1' % loopdev],
                                    msg="format partition %sp1" % loopdev)
        except KeyboardInterrupt:
            retval = 127

        # Disconnect loopdevice:
        finally:
            utils.sudo(['losetup', '-d', loopdev],
                          msg="detach from loop device %s" % loopdev)

        # Evaluate progress:
        if retval != 0:
            exit(retval)


    def create(self, size: int, iso_path: str, **kwargs: dict):
        if not os.path.exists(iso_path):
            print("%s: ISO image not found" % iso_path, file=sys.stderr)
            exit(1)

        self.prepare(size, kwargs['force'])

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
        subprocess.call(qemu_cmd)


    def install(self, size: int, os_name: str, **kwargs: dict):
        self.prepare(size, kwargs['force'])
        { 'Debian': VM.install_debian }[os_name](self, size)


    def install_debian(self, size: int):
        self.partition()
        self.format('ext4')
        try:
            utils.sudo(['%s/debian.sh' % INSTALLSCRIPTS_ROOT, self.path],
                       sudo_msg='debootstrap', stdout=True, stderr=True)
        except KeyboardInterrupt:
            exit(127)


    def delete(self, **kwargs: dict):
        if not self.exists():
            print("Machine [%s] does not exist" % self.name)
            exit(1)
        try:
            os.unlink(self.path)
        except PermissionError:
            print("Permission denied", file=sys.stderr)
            exit(1)


    def list(self, **kwargs: dict):
        for f in os.listdir(VMROOT):
            bn, ext = os.path.splitext(f)
            if not ext == '.raw':
                continue
            print(bn)


    @staticmethod
    def main(argv: [str]):
        p = argparse.ArgumentParser(description="Handle Virtual Machines", prog=INVOKENAME)

        pcmd = p.add_subparsers(dest="Action")
        pcmd.required = True

        # create
        pcreate = pcmd.add_parser('create',
                                  help="Install an OS from an ISO to a VM")
        pcreate.set_defaults(action=VM.create)
        pcreate.add_argument('-f','--force', action='store_true', default=False,
                             help="Force creation, even if VM already exists")
        pcreate.add_argument('iso_path',
                             help="Path to ISO file containing the OS")
        pcreate.add_argument('name',
                             help="Machine name")
        pcreate.add_argument('size', type=int, default=5120, nargs='?',
                             help="VM size (in MB)")

        # install
        pinstall = pcmd.add_parser('install',
                                   help="Install a prepared OS to a VM")
        pinstall.set_defaults(action=VM.install)
        pinstall.add_argument('-f','--force', action='store_true',default=False,
                             help="Force installation, even if already exists")
        pinstall.add_argument('os_name', choices=PREPARED,
                              help="Operating System name")
        pinstall.add_argument('name',
                              help="Machine name")
        pinstall.add_argument('size', type=int, default=5120, nargs='?',
                              help="VM size (in MB)")

        # delete
        pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
        pdelete.set_defaults(action=VM.delete)
        pdelete.add_argument('name', help="Machine name")

        # list
        plist = pcmd.add_parser('list', help="List existing VMs")
        plist.set_defaults(action=VM.list)

        args = p.parse_args(argv[1:])
        kwargs = vars(args) # make it a dictionary, for easier use

        #if kwargs['action'] == VM.list:
        #    VM.list()
        #else:
        vm = VM(kwargs.get('name', None))
        return kwargs['action'](vm, **kwargs)


if __name__ == '__main__':
    VM.main(sys.argv)
