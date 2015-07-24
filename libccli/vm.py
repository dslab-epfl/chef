#!/usr/bin/env python3

import os
import argparse
import sys
import psutil
import utils
import tempfile
import subprocess
import signal
import shutil


DATAROOT = os.environ.get('CHEF_DATAROOT', '/var/local/chef')
INVOKENAME = os.environ.get('INVOKENAME', sys.argv[0])
SRC_ROOT = os.path.dirname(os.path.dirname(__file__))
VMROOT = '%s/vm' % DATAROOT
PREPARED = {'Debian':{'iso':'debian-7.8.0-i386-netinst.iso'}}
FETCH_URL_BASE = 'http://localhost/~ayekat' # TODO real host


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s' % (VMROOT, name)
        self.path_raw = '%s/disk.raw' % (self.path)
        self.path_s2e = '%s/disk.s2e' % (self.path)
        self.path_qcow = '%s/disk.qcow2' % (self.path)


    # UTILITIES ================================================================

    def exists(self):
        return self.name and os.path.isdir(self.path_raw)


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
            open(self.path_raw, 'a').close()
        except PermissionError:
            utils.fail("Permission denied")
            exit(1)
        if utils.execute(['qemu-img', 'create', self.path_raw, '%dM' % size],
                         msg="execute qemu-img") != 0:
            exit(1)
        utils.set_permissions(self.path_raw)
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
                    '-drive', 'file=%s,if=virtio,format=raw' % self.path_raw,
                    '-drive', 'file=%s,media=cdrom,readonly' % iso_path,
                    '-boot', 'order=d']
        print("executing: `%s`" % ' '.join(qemu_cmd))
        subprocess.call(qemu_cmd)

        utils.ok()
        utils.set_msg_prefix(None)


    def fetch(self, os_name: str, **kwargs: dict):
        remote_tar_gz = '%s.tar.gz' % os_name
        remote_qcow = os.path.basename(self.path_qcow)
        remote_iso = PREPARED[os_name]['iso']
        self.path_iso = '%s/%s' % (VMROOT, remote_iso)
        self.path_tar_gz = '%s/%s' % (self.path, remote_tar_gz)

        # Prepare
        utils.set_msg_prefix("create directory")
        utils.pend()
        try:
            os.mkdir(self.path)
        except OSError as ce:
            msg = "%s already exists" % self.name
            if kwargs['force']:
                utils.info("%s, overwriting" % msg)
                try:
                    shutil.rmtree(self.path)
                    os.mkdir(self.path)
                except OSError as re:
                    fail("%s" % re, eol='')
                    exit(1)
            else:
                utils.info(msg)
                exit(1)
        utils.set_msg_prefix(None)

        # Fetch
        url = '%s/%s' % (FETCH_URL_BASE, remote_tar_gz)
        utils.info("URL: %s" % url)
        utils.fetch(url, self.path_tar_gz, unit=utils.MEBI, msg="fetch image bundle")

        # Extract
        utils.set_msg_prefix("extract bundle")
        mapping = {remote_qcow: self.path_qcow,
                   remote_iso: self.path_iso}
        for remote in mapping:
            local = mapping[remote]
            msg = '%s => %s' % (remote, local)
            utils.pend(msg)
            if utils.execute(['tar', '-z', '-f', self.path_tar_gz,
                              '-x', remote, '-O'],
                             msg="extract", outfile=local) != 0:
                exit(1)
            utils.ok(msg)

        # Expand
        utils.set_msg_prefix("expand image")
        utils.pend()
        if utils.execute(['qemu-img', 'convert', '-f', 'qcow2',
                          '-O', 'raw', self.path_qcow, self.path_raw],
                          msg="expand qemu image") != 0:
            exit(1)
        utils.ok()

        # Symlink
        utils.set_msg_prefix("create S2E image")
        utils.pend()
        dest = os.path.basename(self.path_raw)
        if utils.execute(['ln', '-fs', os.path.basename(self.path_raw),
                          self.path_s2e], msg="symlink") != 0:
            exit(1)
        utils.ok()

        utils.set_msg_prefix(None)


    def delete(self, **kwargs: dict):
        if not self.exists():
            utils.fail("Machine [%s] does not exist" % self.name)
            exit(1)
        try:
            os.unlink(self.path_raw)
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
        prepared_list = list(PREPARED)
        pfetch = pcmd.add_parser('fetch',
                                 help="Download a prepared OS image")
        pfetch.set_defaults(action=VM.fetch)
        pfetch.add_argument('-f','--force', action='store_true', default=False,
                            help="Overwrite existing OS image")
        pfetch.add_argument('os_name', choices=prepared_list,
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
