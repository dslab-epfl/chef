#!/usr/bin/env python3

import os
import argparse
import sys
import psutil
import utils
import tempfile
import subprocess
import signal


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


    # UTILITIES ================================================================

    def exists(self):
        return self.name and os.path.exists(self.path)


    def prepare(self, size: int, force: bool):
        utils.set_msg_prefix("prepare disk image")
        utils.pend(pending=True)

        if self.exists():
            msg = "Machine [%s] already exists" % self.name
            if force:
                utils.skip(msg)
                return
            else:
                utils.fail(msg)
                exit(1)
        if not os.path.isdir(VMROOT):
            utils.fail("%s: Directory not found (you may need to initialise Chef first)"
                       % VMROOT)
            exit(1)
        try:
            open(self.path, 'a').close()
        except PermissionError:
            utils.fail("Permission denied")
            exit(1)
        if utils.execute(['qemu-img', 'create', self.path, '%dM' % size],
                         msg="execute qemu-img") != 0:
            exit(1)
        utils.set_permissions(self.path)
        utils.ok()
        utils.set_msg_prefix(None)


    def format(self, fs:str='ext4'):
        utils.set_msg_prefix("format disk image")
        utils.pend(pending=True)

        # Format:
        try:
            out,_,retval = utils.sudo(['blkid', '-o', 'value', '-s', 'TYPE',
                                      '%s' % self.path], iowrap=True)
            if retval == 0 and out.strip() == fs:
                utils.skip("already formatted as %s" % fs)
            else:
                if utils.sudo(['mkfs.%s' % fs, self.path],
                            msg="format partition %s" % self.path) != 0:
                    exit(1)
                utils.ok()
        except KeyboardInterrupt:
            utils.fail("Keyboard interrupt")
            exit(127)

        utils.set_msg_prefix(None)


    # ACTIONS ==================================================================

    def create(self, size: int, iso_path: str, **kwargs: dict):
        utils.set_msg_prefix("create")
        utils.pend(pending=True)

        if not os.path.exists(iso_path):
            utils.fail("%s: ISO image not found" % iso_path, file=sys.stderr)
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

        utils.ok()
        utils.set_msg_prefix(None)


    def install(self, size: int, os_name: str, **kwargs: dict):
        def install_debian(self, size: int):
            self.format('ext4')

            utils.set_msg_prefix("install Debian")
            utils.pend()
            if utils.sudo(['%s/debian.sh' % INSTALLSCRIPTS_ROOT, self.path],
                         sudo_msg='debootstrap', stdout=True, stderr=True) != 0:
                utils.fail()
            else:
                utils.ok()
            utils.set_msg_prefix(None)

        self.prepare(size, kwargs['force'])
        { 'Debian': install_debian }[os_name](self, size)


    def delete(self, **kwargs: dict):
        if not self.exists():
            utils.fail("Machine [%s] does not exist" % self.name)
            exit(1)
        try:
            os.unlink(self.path)
        except PermissionError:
            utils.fail("Permission denied")
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
