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
import shutil
import re

class VM:
    cores = os.cpu_count()
    memory = min(max(psutil.virtual_memory().total / 4, 2 * 1024), 4 * 1024)


    def __init__(self, name: str, must_exist: bool):
        self.name = name
        self.path = '%s/%s' % (utils.CHEFROOT_VM, name)
        self.path_raw = '%s/disk.s2e' % self.path
        self.scan_snapshots()
        self.path_executable = '%s/%s-%s-%s/qemu' \
                  % (utils.CHEFROOT_BUILD, utils.ARCH, utils.TARGET, utils.MODE)
        self.size = os.stat(self.path_raw).st_size if self.exists() else 0
        if not self.exists() and must_exist:
            fail("%s: VM does not exist" % name)
            exit(1)


    def scan_snapshots(self):
        self.snapshots = []
        if not self.exists():
            return
        for name in os.listdir(self.path):
            snapshot = re.search(
                '(?<=%s\.).+' % os.path.basename(self.path_raw),
                name
            )
            if snapshot:
                self.snapshots.append(snapshot.group(0))


    def __str__(self):
        string = "%s%s%s" % (utils.ESC_BOLD, self.name, utils.ESC_RESET)
        if self.size > 0:
            string += "\n  Size: %.1fMiB" % (self.size / utils.MEBI)
        if self.snapshots:
            string += "\n  Snapshots:"
            for snapshot in self.snapshots:
                string += "\n    %s" % snapshot
        return string


    # UTILITIES ================================================================

    def exists(self):
        return os.path.exists(self.path_raw)


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

    # ACTIONS ==================================================================

    def create(self, size: str, force: bool, **kwargs: dict):
        self.initialise(force)

        # Raw image:
        utils.pend("create %sB image" % size)
        utils.execute(['%s/qemu-img' % self.path_executable,
                       'create', self.path_raw, size],
                      msg="execute qemu-img")
        self.size = size
        utils.ok()


    def export(self, targz: str, **kwargs: dict):
        if not targz:
            targz = '%s.tar.gz' % self.name
        targz = '%s/%s' % (os.path.dirname(targz) or os.getcwd(), os.path.basename(targz))
        tar, _ = os.path.splitext(targz)
        if os.path.exists(tar):
            utils.fail("%s: tarball already exists" % tar)
            exit(1)
        if os.path.exists(targz):
            utils.fail("%s: gzipped tarball already exists" % targz)
            exit(1)

        os.chdir(self.path)  # create intermediate files in VM's path

        utils.pend("convert disk image")
        local_qcow = '%s/disk.qcow2' % self.path
        utils.execute(['%s/qemu-img' % self.path_executable, 'convert',
                       self.path_raw, '-f', 'raw', '-O', 'qcow2', local_qcow])
        utils.ok()

        utils.pend("package disk image")
        utils.execute(['tar', 'cf', tar, local_qcow])
        utils.ok()

        for s in self.snapshots:
            utils.pend("package snapshot: %s" % s)
            utils.execute(['tar', 'rf', tar, '%s.%s' % (self.path_raw, s)])
            utils.ok()

        utils.pend("clean up")
        os.unlink(local_qcow)
        utils.ok()

        utils.pend("compress tarball", msg="(may take some time)")
        utils.execute(['gzip', tar])
        utils.ok()


    def clone(self, clone: str, force: bool, **kwargs: dict):
        if self.name == clone:
            utils.fail("%s: please specify a different name" % clone)
            exit(2)

        new = VM(clone, must_exist=False)
        new.initialise(force)

        utils.pend("copy disk image", msg="(may take some time)")
        try:
            shutil.copy(self.path_raw, new.path_raw)
            utils.ok()
        except KeyboardInterrupt as ki:
            utils.abort("%s" % ki)
            exit(127)

        for s in self.snapshots:
            utils.pend("copy snapshot: %s" % s)
            try:
                shutil.copy('%s/%s.%s'
                            % (self.path, os.path.basename(self.path_raw), s),
                            new.path)
                utils.ok()
            except KeyboardInterrupt as ki:
                utils.abort("%s" % ki)
                exit(127)
        new.scan_snapshots()


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


    def list(self, **kwargs: dict):
        for name in os.listdir(utils.CHEFROOT_VM):
            if not os.path.isdir('%s/%s' % (utils.CHEFROOT_VM, name)):
                continue
            print(VM(name=name, must_exist=True))

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
        pcreate.set_defaults(must_exist=False)
        pcreate.add_argument('-f','--force', action='store_true', default=False,
                             help="Force creation, even if VM already exists")
        pcreate.add_argument('name',
                             help="Machine name")
        pcreate.add_argument('size', default='5120M', nargs='?',
                             help="VM size [default=5120M]")

        # export
        pexport = pcmd.add_parser('export',
                                  help="Export VM as a gzipped Tarball")
        pexport.set_defaults(action=VM.export)
        pexport.set_defaults(must_exist=True)
        pexport.add_argument('name',
                             help="Machine name")
        pexport.add_argument('targz', default=None, nargs='?',
                             help="Tarball name [default=./[Machine name].tar.gz]")

        # clone
        pclone = pcmd.add_parser('clone',
                                 help="Create an exact copy of an existing VM")
        pclone.set_defaults(action=VM.clone)
        pclone.set_defaults(must_exist=True)
        pclone.add_argument('-f','--force', action='store_true', default=False,
                            help="Force cloning, even if VM already exists")
        pclone.add_argument('name',
                            help="Machine name (original)")
        pclone.add_argument('clone',
                            help="Machine name (clone)")

        # delete
        pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
        pdelete.set_defaults(action=VM.delete)
        pdelete.set_defaults(must_exist=True)
        pdelete.add_argument('name', help="Machine name")

        # list
        plist = pcmd.add_parser('list', help="List VMs")
        plist.set_defaults(action=VM.list)
        plist.set_defaults(must_exist=False)

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
    # Parse command line arguments:
    args = VM.parse_args(sys.argv)
    utils.parse_release(args['release'])

    # Check environment:
    if not os.path.isdir(utils.CHEFROOT_VM):
        VM.vm_init(utils.CHEFROOT_VM)

    # Create VM and handle action:
    vm = VM(name=args.get('name', None), must_exist=args['must_exist'])
    exit(args['action'](vm, **args))
