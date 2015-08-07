#!/usr/bin/env python3

# This script is part of the Chef command line tools.
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

FETCH_URL_BASE = 'http://localhost/~ayekat' # TODO real host

# TODO split "repository" from script
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
    cores = os.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s' % (utils.CHEFROOT_VM, name)
        self.path_qcow = '%s/disk.qcow2' % self.path
        self.path_raw = '%s/disk.raw' % self.path
        self.path_s2e = '%s/disk.s2e' % self.path
        self.path_meta = '%s/meta' % self.path
        self.path_defunct = '%s/defunct' % self.path
        self.defunct = os.path.exists(self.path_defunct)
        self.path_executable = '%s/%s-%s-%s/qemu' \
                  % (utils.CHEFROOT_BUILD, utils.ARCH, utils.TARGET, utils.MODE)
        self.load_meta()
        self.scan_snapshots()
        self.size = os.stat(self.path_raw).st_size if self.exists() else 0


    def load_meta(self):
        meta = {}
        if os.path.exists(self.path_meta):
            with open(self.path_meta, 'r') as f:
                try:
                    meta = json.load(f)
                except ValueError as ve:
                    utils.warn(ve)
        self.path_tar_gz = meta.get('path_tar_gz', None)
        self.os_name = meta.get('os_name', None)
        self.description = meta.get('description', None)


    def store_meta(self):
        utils.pend("store metadata")
        meta = {
            'path_tar_xz': self.path_tar_gz,
            'os_name': self.os_name,
            'description': self.description
        }
        with open(self.path_meta, 'w') as f:
            json.dump(meta, f)
        utils.ok()


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
        if self.defunct:
            string += "\n  %s<defunct>%s" % (utils.ESC_ERROR, utils.ESC_RESET)
            return string
        if self.size > 0:
            string += "\n  Size: %.1fMiB" % (self.size / utils.MEBI)
        if self.os_name:
            string += "\n  Operating System: %s" % self.os_name
        if self.snapshots:
            string += "\n  Snapshots:"
            for snapshot in self.snapshots:
                string += "\n    %s" % snapshot
        return string


    # UTILITIES ================================================================

    def exists(self):
        return os.path.isdir(self.path) \
           and os.path.exists(self.path_raw) \
           and os.path.exists(self.path_s2e)


    def initialise(self, force: bool):
        utils.pend("initialise VM")
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


    def create_s2e(self):
        utils.pend("symlink S²E image")
        dest = os.path.basename(self.path_raw)
        exists = os.path.exists(self.path_s2e)
        if exists:
            dest_real = os.path.readlink(self.path_s2e)
        invalid = exists and dest != dest_real
        if not exists or invalid:
            if utils.execute(['ln', '-fs', dest, self.path_s2e],
                             msg="symlink") != 0:
                self.mark_defunct()
                exit(1)
            if invalid:
                utils.note("fix invalid S²E image (pointed")
            else:
                utils.ok()


    def mark_defunct(self):
        self.defunct = True
        open(self.path_defunct, 'w').close()


    # ACTIONS ==================================================================

    def create(self, size: int, force: bool, **kwargs: dict):
        self.initialise(force)

        # Raw image:
        utils.pend("create %dMiB image" % size)
        if utils.execute(['%s/qemu-img' % self.path_executable,
                          'create', self.path_raw, '%dM' % size],
                         msg="execute qemu-img") != 0:
            exit(1)
        self.size = size
        utils.ok()

        # S2E image:
        self.create_s2e()

        # Metadata:
        self.store_meta()


    def install(self, iso_path: str, **kwargs: dict):
        if not self.exists():
            utils.fail("%s: VM does not exist" % self.name)
            exit(1)
        if not os.path.exists(iso_path):
            utils.fail("%s: ISO file not found" % iso_path)
            exit(1)

        # Launch qemu:
        qemu_cmd = ['%s/qemu-system-%s' % (self.path_executable, utils.ARCH),
                    '-enable-kvm',
                    '-cpu', 'host',
                    '-smp', '%d' % VM.cores,
                    '-m', '%d' % VM.memory,
                    '-vga', 'std',
                    '-monitor', 'tcp::1234,server,nowait',
                    '-drive', 'file=%s,if=virtio,format=raw' % self.path_raw,
                    '-drive', 'file=%s,media=cdrom,readonly' % iso_path,
                    '-boot', 'order=d']
        utils.info("qemu: command line\n%s" % ' '.join(qemu_cmd))
        utils.pend("qemu", pending=False)
        if utils.execute(qemu_cmd, msg="run qemu", stdout=True, stderr=True) != 0:
            exit(1)
        utils.ok()


    def fetch(self, os_name: str, force: bool, **kwargs: dict):
        self.os_name = os_name
        self.description = REMOTES[os_name]['description']
        remote_iso = REMOTES[os_name]['iso']
        iso_path = '%s/%s' % (utils.CHEFROOT_VM, remote_iso)
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
            self.mark_defunct()
            exit(1)

        # Extract:
        utils.pend("extract bundle")
        mapping = {remote_qcow: self.path_qcow,
                   remote_iso: iso_path}
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
                    self.mark_defunct()
                    exit(1)
                utils.ok(msg)

        # Raw image:
        utils.pend("expand raw image")
        if utils.execute(['qemu-img', 'convert', '-f', 'qcow2',
                          '-O', 'raw', self.path_qcow, self.path_raw],
                          msg="expand qemu image") != 0:
            self.mark_defunct()
            exit(1)
        utils.ok()

        # S2E image:
        self.create_s2e()

        # Metadata:
        self.store_meta()


    def delete(self, **kwargs: dict):
        utils.pend("delete %s" % self.name)
        try:
            shutil.rmtree(self.path)
        except PermissionError:
            utils.fail("Permission denied")
            exit(1)
        except FileNotFoundError:
            utils.fail("VM does not exist")
            exit(1)
        utils.ok()


    def list(self, local: bool, remote: bool, filter: str=None, **kwargs: dict):
        if remote:
            for name in REMOTES:
                print(name)
                print("  %s" % REMOTES[name]['description'])
                print("  Based on: %s" % REMOTES[name]['iso'])
        elif local:
            for name in os.listdir(utils.CHEFROOT_VM):
                if not os.path.isdir('%s/%s' % (utils.CHEFROOT_VM, name)):
                    continue
                print(VM(name))
        else:
            internal_error("list(): neither local nor remote list chosen")
            exit(127)

    # MAIN =====================================================================

    @staticmethod
    def parse_args(argv: [str]):
        p = argparse.ArgumentParser(description="Handle Virtual Machines",
                                    prog=utils.INVOKENAME)
        p.add_argument('-r', '--release', type=str, default=utils.RELEASE,
                       help="Release tuple architecture:target:mode")

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
        plist_source.add_argument('-r', '--remote', action='store_true',
                                  default=False,
                                  help="List remotely available VMs")

        args = p.parse_args(argv[1:])
        return vars(args) # make it a dictionary, for easier use


    @staticmethod
    def vm_init(path: str):
        utils.pend("initialise VM directory: %s" % path)
        try:
            os.mkdir(path)
        except OSError as e:
            utils.fail(e.strip())
            exit(1)
        utils.ok()


if __name__ == '__main__':
    # Check environment:
    if not os.path.isdir(utils.CHEFROOT_VM):
        VM.vm_init(utils.CHEFROOT_VM)

    # Parse command line arguments:
    kwargs = VM.parse_args(argv)
    utils.parse_release(kwargs['release'])

    # Create VM and handle action:
    vm = VM(kwargs.get('name', None))
    exit(kwargs['action'](vm, **kwargs))
