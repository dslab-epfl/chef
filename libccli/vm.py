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
PREPARED = ['Debian']
FETCH_URL_BASE = 'http://localhost/~ayekat' # TODO real host


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s.raw' % (VMROOT, name)
        self.path_qcow = '%s/%s.qcow2' % (VMROOT, name)
        self.path_qcow_gz = '%s.gz' % (self.path_qcow)


    # UTILITIES ================================================================

    def exists(self):
        return self.name and os.path.exists(self.path)


    def prepare(self, size: int, force: bool):
        utils.set_msg_prefix("prepare disk image")
        utils.pend()

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


    # ACTIONS ==================================================================

    def create(self, size: int, iso_path: str, **kwargs: dict):
        utils.set_msg_prefix("create")
        utils.pend()

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


    def fetch(self, os_name: str, **kwargs: dict):
        if self.exists():
            msg = "Machine [%s] already exists" % self.name
            if kwargs['force']:
                utils.warn(msg)
            else:
                utils.fail(msg)
                exit(1)
        if not os.path.exists(self.path_qcow) or kwargs['no_cache']:
            # Fetch
            url = '%s/%s.qcow2.gz' % (FETCH_URL_BASE, os_name)
            utils.fetch(url, self.path_qcow_gz, overwrite=kwargs['no_cache'],
                        unit=utils.MEBI, msg="fetch disk image")

            # Extract
            utils.set_msg_prefix("extract image")
            utils.pend()
            try:
                if utils.execute(['gunzip', self.path_qcow_gz], msg="gunzip") != 0:
                    exit(1)
            except KeyboardInterrupt:
                utils.abort("keyboard interrupt")
                exit(127)
            utils.ok()

        # Convert:
        utils.set_msg_prefix("convert image")
        utils.pend()
        try:
            if utils.execute(['qemu-img', 'convert', '-f', 'qcow2', '-O', 'raw',
                             self.path_qcow, self.path],
                             msg="convert qemu image") != 0:
                exit(1)
        except KeyboardInterrupt:
            utils.abort("keyboard interrupt")
            exit(127)
        utils.ok()
        utils.set_msg_prefix(None)


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

    # MAIN =====================================================================

    @staticmethod
    def main(argv: [str]):
        p = argparse.ArgumentParser(description="Handle Virtual Machines",
                                    prog=INVOKENAME)

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

        # fetch
        pfetch = pcmd.add_parser('fetch',
                                 help="Download a prepared OS image")
        pfetch.set_defaults(action=VM.fetch)
        pfetch.add_argument('-f','--force', action='store_true', default=False,
                            help="Overwrite existing OS image")
        pfetch.add_argument('--no-cache', action='store_true', default=False,
                            help="Don't use locally cached download files")
        pfetch.add_argument('os_name', choices=PREPARED,
                            help="Operating System name")
        pfetch.add_argument('name',
                            help="Machine name")

        # delete
        pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
        pdelete.set_defaults(action=VM.delete)
        pdelete.add_argument('name', help="Machine name")

        # list
        plist = pcmd.add_parser('list', help="List existing VMs")
        plist.set_defaults(action=VM.list)

        args = p.parse_args(argv[1:])
        kwargs = vars(args) # make it a dictionary, for easier use

        vm = VM(kwargs.get('name', None))
        return kwargs['action'](vm, **kwargs)


if __name__ == '__main__':
    VM.main(sys.argv)
