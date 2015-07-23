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
PREPARED = {'Debian':'debian-7.8.0-i386-netinst.iso'}
FETCH_URL_BASE = 'http://localhost/~ayekat' # TODO real host


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path_raw = '%s/%s.raw' % (VMROOT, name)
        self.path_s2e = '%s/%s.s2e' % (VMROOT, name)
        self.path_qcow = '%s/%s.qcow2' % (VMROOT, name)
        self.path_iso = '%s/%s' % (VMROOT, PREPARED[name])
        self.path_tar_gz = '%s/%s.tar.gz' % (VMROOT, name)


    # UTILITIES ================================================================

    def exists(self):
        return self.name and os.path.exists(self.path_raw)


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
        if not os.path.exists(self.path_qcow) \
        or not os.path.exists(self.path_iso) \
        or kwargs['no_cache']:
            # Fetch
            url = '%s/%s' % (FETCH_URL_BASE, os.path.basename(self.path_tar_gz))
            utils.fetch(url, self.path_tar_gz, overwrite=kwargs['no_cache'],
                        unit=utils.MEBI, msg="fetch image bundle")
        elif not os.path.exists(self.path_tar_gz):
            utils.warn('no local version of archive available')

        # Extract
        utils.set_msg_prefix("extract bundle")
        for f in [self.path_qcow, self.path_iso]:
            fb = os.path.basename(f)
            if not os.path.exists(f) or kwargs['no_cache']:
                utils.pend('%s' % fb)
                if utils.execute(['tar', '-z', '-x', fb,
                                  '-f', self.path_tar_gz],
                                  msg="extract") != 0:
                    exit(1)
                utils.ok('%s' % fb)
            else:
                utils.skip('%s: already extracted' % fb)

        # Expand:
        utils.set_msg_prefix("expand image")
        utils.pend()
        if not self.exists() or kwargs['force']:
            if utils.execute(['qemu-img', 'convert', '-f', 'qcow2',
                              '-O', 'raw', self.path_qcow, self.path_raw],
                              msg="expand qemu image") != 0:
                exit(1)
            utils.ok()
        else:
            utils.skip("%s already exists" % self.path_raw)

        # Symlink:
        utils.set_msg_prefix("create S2E image")
        utils.pend()
        dest = os.path.basename(self.path_raw)
        exists = os.path.exists(self.path_s2e)
        if exists:
            current_dest = os.readlink(self.path_s2e)
            overwrite = current_dest != dest
        if not exists or overwrite:
            if overwrite:
                utils.warn("overwriting existing symbolic link %s"
                           % self.path_s2e)
            if utils.execute(['ln', '-fs', os.path.basename(self.path_raw),
                              self.path_s2e], msg="symlink") != 0:
                exit(1)
            if not overwrite:
                utils.ok()
        else:
            utils.skip("%s already symlinked" % self.path_s2e)

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
        pfetch.add_argument('--no-cache', action='store_true', default=False,
                            help="Don't use locally cached download files")
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
