#!/usr/bin/env python3

# This script is part of ccli.
#
# `vm` provides means for managing virtual machines that are used with Chef. The
# virtual machines are basically raw disk image files containing operating
# systems that can be booted by qemu/S²E/Chef.
#
# Maintainers:
#   Tinu Weber <martin.weber@epfl.ch>


import os
import argparse
import sys
import psutil
import utils
import tempfile
import subprocess
import signal
import shutil
import json
import re

INVOKENAME = os.environ.get('INVOKENAME', sys.argv[0])
FETCH_URL_BASE = 'http://localhost/~ayekat' # TODO real host
REMOTES = {
    'Debian': {
        'iso': 'debian-7.8.0-i386-netinst.iso',
        'description': "Debian 7.8 (Wheezy) with a custom kernel, prepared " +
                       "for being used with Chef"
    },
    'Ubuntu': {
        'iso': 'ubuntu-14.04-desktop-i386.iso',
        'description': "Ubuntu 14.04 (Trusty Tahr)"
    },
}


class VM:
    arch = 'x86_64'
    cores = psutil.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s' % (utils.DATAROOT_VM, name)
        self.path_qcow = '%s/disk.qcow2' % self.path
        self.path_raw = '%s/disk.raw' % self.path
        self.path_s2e = '%s/disk.s2e' % self.path
        self.path_meta = '%s/meta' % self.path
        self.path_dysfunct = '%s/dysfunct' % self.path
        self.dysfunct = os.path.exists(self.path_dysfunct)
        self.load_meta()
        self.scan_snapshots()


    def load_meta(self):
        meta = {}
        if os.path.exists(self.path_meta):
            with open(self.path_meta, 'r') as f:
                try:
                    meta = json.load(f)
                except ValueError as ve:
                    utils.warn(ve)
        utils.set_msg_prefix(None)
        self.path_tar_gz = meta.get('path_tar_gz', None)
        self.path_iso = meta.get('path_iso', None)
        self.os_name = meta.get('os_name', None)
        self.description = meta.get('description', None)


    def store_meta(self):
        utils.set_msg_prefix("store metadata")
        utils.pend()
        meta = {
            'path_tar_xz': self.path_tar_gz,
            'path_iso': self.path_iso,
            'os_name': self.os_name,
            'description': self.description
        }
        with open(self.path_meta, 'w') as f:
            json.dump(meta, f)
        utils.ok()
        utils.set_msg_prefix(None)


    def scan_snapshots(self):
        self.snapshots = []
        if not os.path.isdir(self.path):
            return
        for name in os.listdir(self.path):
            snapshot = re.search('(?<=disk.s2e.).+', name)
            if snapshot:
                self.snapshots.append(snapshot.group(0))


    def __str__(self):
        string = "%s" % self.name
        if self.dysfunct:
            string += "\n  %s<dysfunct>%s" % (utils.ESC_ERROR, utils.ESC_RESET)
            return string
        if self.os_name:
            string += "\n  Operating System: %s" % self.os_name
        if self.path_iso:
            string += "\n  Based on: %s" % self.path_iso
        if self.snapshots:
            string += "\n  Snapshots:"
            for snapshot in self.snapshots:
                string += "\n    %s" % snapshot
        return string


    # UTILITIES ================================================================

    def exists(self):
        return self.name \
        and os.path.isdir(self.path) \
        and os.path.exists(self.path_raw) \
        and os.path.exists(self.path_s2e)


    def initialise(self, force: bool):
        utils.set_msg_prefix("initialise VM")
        utils.pend()
        try:
            os.mkdir(self.path)
            utils.ok()
        except PermissionError as pe:
            utils.fail(pe)
            exit(1)
        except OSError as ose:
            msg = "%s already exists" % self.name
            if force:
                utils.info("%s, overwriting" % msg)
                try:
                    shutil.rmtree(self.path) # FIXME what if PWD == self.path ?
                    os.mkdir(self.path)
                except PermissionError as pe:
                    utils.fail(pe)
                    exit(1)
                except OSError as ose2:
                    utils.fail(ose)
                    exit(1)
            else:
                utils.info(msg)
                exit(1)
        utils.set_msg_prefix(None)


    def create_s2e(self):
        utils.set_msg_prefix("symlink S2E image")
        utils.pend()
        dest = os.path.basename(self.path_raw)
        exists = os.path.exists(self.path_s2e)
        if exists:
            dest_real = os.path.readlink(self.path_s2e)
        invalid = exists and dest != dest_real
        if not exists or invalid:
            if utils.execute(['ln', '-fs', dest, self.path_s2e],
                             msg="symlink") != 0:
                self.mark_dysfunct()
                exit(1)
            if invalid:
                utils.note("fix invalid S2E image (pointed")
            else:
                utils.ok()

        utils.set_msg_prefix(None)


    def set_permissions(self):
        for f in [self.path, self.path_raw]:
            utils.set_permissions(f)


    def mark_dysfunct(self):
        self.dysfunct = True
        open(self.path_dysfunct, 'w').close()


    # ACTIONS ==================================================================

    def create(self, size: int, force: bool, **kwargs: dict):
        self.initialise(force)

        # Raw image:
        utils.set_msg_prefix("create %dMiB image" % size)
        utils.pend()
        if utils.execute(['qemu-img', 'create', self.path_raw, '%dM' % size],
                         msg="execute qemu-img") != 0:
            exit(1)
        utils.ok()
        utils.set_msg_prefix(None)

        # S2E image:
        self.create_s2e()

        # Metadata:
        self.store_meta()

        # Permissions:
        self.set_permissions()


    def install(self, iso_path: str, **kwargs: dict):
        if not self.exists():
            utils.fail("%s: VM does not exist" % self.name)
            exit(1)
        if not os.path.exists(iso_path):
            utils.fail("%s: ISO file not found" % iso_path)
            exit(1)

        # Copy ISO:
        self.path_iso = '%s/%s' % (utils.DATAROOT_VM,
                                   os.path.basename(iso_path))
        utils.set_msg_prefix("register ISO")
        utils.pend("%s => %s" % (iso_path, self.path_iso))
        if not os.path.exists(self.path_iso):
            try:
                shutil.copy(iso_path, self.path_iso)
            except PermissionError as pe:
                utils.fail(pe)
                exit(1)
            except OSError as ose:
                utils.fail(ose)
                exit(1)
            utils.ok()
        else:
            utils.skip("%s already exists" % self.path_iso)

        # Launch qemu:
        utils.set_msg_prefix("qemu")
        qemu_cmd = ['qemu-system-%s' % VM.arch,
                    '-enable-kvm',
                    '-cpu', 'host',
                    '-smp', '%d' % VM.cores,
                    '-m', '%d' % VM.memory,
                    '-vga', 'std',
                    '-net', 'user',
                    '-monitor', 'tcp::1234,server,nowait',
                    '-drive', 'file=%s,if=virtio,format=raw' % self.path_raw,
                    '-drive', 'file=%s,media=cdrom,readonly' % self.path_iso,
                    '-boot', 'order=d']
        utils.info("command line\n%s" % ' '.join(qemu_cmd))
        utils.pend(pending=False)
        if utils.execute(qemu_cmd, msg="run qemu", stdout=True, stderr=True) != 0:
            exit(1)
        utils.ok()


    def fetch(self, os_name: str, force: bool, **kwargs: dict):
        self.os_name = os_name
        self.description = REMOTES[os_name]['description']
        remote_iso = REMOTES[os_name]['iso']
        self.path_iso = '%s/%s' % (utils.DATAROOT_VM, remote_iso)
        remote_qcow = os.path.basename(self.path_qcow)
        remote_tar_gz = '%s.tar.gz' % os_name
        self.path_tar_gz = '%s/%s' % (self.path, remote_tar_gz)

        # Initialise:
        self.initialise(force)

        # Fetch:
        url = '%s/%s' % (FETCH_URL_BASE, remote_tar_gz)
        utils.info("URL: %s" % url)
        if utils.fetch(url, self.path_tar_gz, unit=utils.MEBI,
                       msg="fetch image bundle") != 0:
            self.mark_dysfunct()
            exit(1)

        # Extract:
        utils.set_msg_prefix("extract bundle")
        mapping = {remote_qcow: self.path_qcow,
                   remote_iso: self.path_iso}
        for remote in mapping:
            local = mapping[remote]
            msg = '%s => %s' % (remote, local)
            utils.pend(msg)
            if os.path.exists(local):
                utils.skip("%s: already extracted" % local)
            else:
                if utils.execute(['tar', '-z', '-f', self.path_tar_gz,
                                  '-x', remote, '-O'],
                                 msg="extract", outfile=local) != 0:
                    self.mark_dysfunct()
                    exit(1)
                utils.ok(msg)

        # Raw image:
        utils.set_msg_prefix("expand raw image")
        utils.pend()
        if utils.execute(['qemu-img', 'convert', '-f', 'qcow2',
                          '-O', 'raw', self.path_qcow, self.path_raw],
                          msg="expand qemu image") != 0:
            self.mark_dysfunct()
            exit(1)
        utils.ok()

        # S2E image:
        self.create_s2e()

        # Metadata:
        self.store_meta()

        # Permissions:
        self.set_permissions()

        utils.set_msg_prefix(None)


    def delete(self, **kwargs: dict):
        utils.set_msg_prefix("delete %s" % self.name)
        utils.pend()
        try:
            shutil.rmtree(self.path)
        except PermissionError:
            utils.fail("Permission denied")
            exit(1)
        except FileNotFoundError:
            utils.fail("VM does not exist")
            exit(1)
        utils.ok()


    def list(self, iso: bool, remote: bool, filter: str=None, **kwargs: dict):
        if remote:
            for name in REMOTES:
                print(name)
                print("  %s" % REMOTES[name]['description'])
                print("  Based on: %s" % REMOTES[name]['iso'])
        else:
            for name in os.listdir(utils.DATAROOT_VM):
                if iso:
                    _, ext = os.path.splitext(name)
                    if ext != '.iso':
                        continue
                    print(name)
                else:
                    if not os.path.isdir('%s/%s' % (utils.DATAROOT_VM, name)):
                        continue
                    print(VM(name))

    # MAIN =====================================================================

    @staticmethod
    def parse_args(argv: [str]):
        p = argparse.ArgumentParser(description="Handle Virtual Machines",
                                    prog=INVOKENAME)

        pcmd = p.add_subparsers(dest="Action")
        pcmd.required = True

        # create
        pcreate = pcmd.add_parser('create', help="Create a new VM")
        pcreate.set_defaults(action=VM.create)
        pcreate.add_argument('-f','--force', action='store_true', default=False,
                             help="Force creation, even if VM already exists")
        pcreate.add_argument('name',
                             help="Machine name")
        pcreate.add_argument('size', type=int, default=5120, nargs='?',
                             help="VM size (in MB) [default=5120]")

        # install
        pinstall = pcmd.add_parser('install',
                                   help="Install an OS from an ISO to a VM")
        pinstall.set_defaults(action=VM.install)
        pinstall.add_argument('iso_path',
                              help="Path to ISO file containing the OS")
        pinstall.add_argument('name',
                              help="Machine name")

        # fetch
        pfetch = pcmd.add_parser('fetch', help="Download a prepared VM")
        pfetch.set_defaults(action=VM.fetch)
        pfetch.add_argument('-f','--force', action='store_true', default=False,
                            help="Overwrite existing OS image")
        pfetch.add_argument('os_name', choices=list(REMOTES),
                            help="Operating System name")
        pfetch.add_argument('name',
                            help="Machine name")

        # delete
        pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
        pdelete.set_defaults(action=VM.delete)
        pdelete.add_argument('name', help="Machine name")

        # list
        plist = pcmd.add_parser('list', help="List VMs and ISOs")
        plist.set_defaults(action=VM.list)
        plist_source = plist.add_mutually_exclusive_group()
        plist_source.add_argument('-l', '--local', action='store_true',
                                  default=True,
                                  help="List locally available VMs [default]")
        plist_source.add_argument('-i', '--iso', action='store_true',
                                  default=False,
                                  help="List locally registered ISOs")
        plist_source.add_argument('-r', '--remote', action='store_true',
                                  default=False,
                                  help="List remotely available VMs")

        args = p.parse_args(argv[1:])
        return vars(args) # make it a dictionary, for easier use


    def vm_init(path: str):
        utils.set_msg_prefix("initialise VM directory: %s" % path)
        utils.pend()
        try:
            os.mkdir(path)
        except OSError as e:
            utils.fail(e.strip())
            exit(1)
        utils.ok()


    @staticmethod
    def main(argv: [str]):
        # Check environment:
        if not os.path.isdir(utils.DATAROOT_VM):
            vm_init()

        # Parse command line arguments:
        kwargs = VM.parse_args(argv)

        # Create VM and handle action:
        vm = VM(kwargs.get('name', None))
        return kwargs['action'](vm, **kwargs)


if __name__ == '__main__':
    VM.main(sys.argv)
