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


    def __init__(self, name: str):
        self.name = name
        self.path = '%s/%s' % (utils.CHEFROOT_VM, name)
        self.path_raw = '%s/disk.s2e' % self.path
        self.path_qcow = '%s/disk.qcow2' % self.path
        self.scan_snapshots()
        self.path_executable = '%s/%s-%s-%s/qemu' \
                  % (utils.CHEFROOT_BUILD, utils.ARCH, utils.TARGET, utils.MODE)
        self.size = 0
        if os.path.exists(self.path_raw):
            self.size = os.stat(self.path_raw).st_size


    def scan_snapshots(self):
        self.snapshots = []
        if not os.path.exists(self.path):
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
        if not os.path.exists(self.path_raw):
            string += '\n  %s<defunct>%s' % (utils.ESC_ERROR, utils.ESC_RESET)
        if self.size > 0:
            string += "\n  Size: %.1fMiB" % (self.size / utils.MEBI)
        if self.snapshots:
            string += "\n  Snapshots:"
            for snapshot in self.snapshots:
                string += "\n    %s" % snapshot
        return string


    # UTILITIES ================================================================

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

        utils.pend("create %sB image" % size)
        utils.execute(['%s/qemu-img' % self.path_executable,
                       'create', self.path_raw, size],
                      msg="execute qemu-img")
        self.size = size
        utils.ok()


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


    def export(self, targz: str, **kwargs: dict):
        if not os.path.isdir(self.path):
            utils.fail("%s: VM does not exist" % self.name)
            exit(1)
        if not targz:
            targz = '%s.tar.gz' % self.name
        targz = os.path.abspath(targz)
        utils.info("exporting to %s" % targz)
        tar = '%s/%s' % (self.path, os.path.basename(os.path.splitext(targz)[0]))
        if os.path.exists(targz):
            utils.fail("%s: gzipped tarball already exists" % targz)
            exit(1)

        os.chdir(self.path)  # create intermediate files in VM's path

        utils.pend("convert disk image")
        utils.execute(['%s/qemu-img' % self.path_executable, 'convert',
                       self.path_raw, '-f', 'raw', '-O', 'qcow2', self.path_qcow])
        utils.ok()

        utils.pend("package disk image")
        utils.execute(['tar', '-cf', tar, os.path.basename(self.path_qcow)])
        utils.ok()

        for s in self.snapshots:
            utils.pend("package snapshot: %s" % s)
            local_snapshot = '%s.%s' % (os.path.basename(self.path_raw), s)
            utils.execute(['tar', '-rf', tar, local_snapshot])
            utils.ok()

        utils.pend("compress tarball", msg="may take some time")
        utils.execute(['gzip', '-c', tar], outfile=targz)
        utils.ok()

        utils.pend("clean up")
        os.unlink(self.path_qcow)
        os.unlink(tar)
        utils.ok()

        self.scan_snapshots()


    def _import(self, targz: str, force: bool, **kwargs: dict):
        if not os.path.exists(targz):
            utils.fail("%s: file not found" % targz)
            exit(1)
        self.initialise(force)

        targz = os.path.abspath(targz)
        tar = '%s/%s' % (self.path, os.path.basename(os.path.splitext(targz)[0]))

        os.chdir(self.path)     # create intermediate files in VM's path

        utils.pend("decompress tarball")
        utils.execute(['gzip', '-cd', targz], outfile=tar)
        utils.ok()

        utils.pend("scan tarball")
        _, file_list, _ = utils.execute(['tar', '-tf', tar], iowrap=True)
        utils.ok()

        file_list = file_list.split()
        for f in file_list:
            if f == os.path.basename(self.path_qcow):
                utils.pend("extract disk image")
            else:
                result = re.search(
                    '(?<=%s\.).+' % os.path.basename(self.path_raw), f
                )
                if not result:
                    utils.warn("misformatted file: %s (skipping)" % f)
                    continue
                snapshotname = result.group(0)
                utils.pend("extract snapshot: %s" % snapshotname)
            utils.execute(['tar', '-x', f, '-f', tar])
            utils.ok()

        utils.pend("convert disk image")
        utils.execute(['%s/qemu-img' % self.path_executable, 'convert',
                       self.path_qcow, '-f','qcow2', '-O','raw', self.path_raw])
        utils.ok()

        utils.pend("clean up")
        os.unlink(self.path_qcow)
        os.unlink(tar)
        utils.ok()


    def clone(self, clone: str, force: bool, **kwargs: dict):
        if not os.path.isdir(self.path):
            utils.fail("%s: VM does not exist" % self.name)
            exit(1)
        if self.name == clone:
            utils.fail("%s: please specify a different name" % clone)
            exit(2)

        new = VM(clone)
        new.initialise(force)

        # http://bugs.python.org/issue10016
        utils.pend("copy disk image", msg="may take some time")
        utils.execute(['cp', self.path_raw, new.path_raw])
        utils.ok()

        for s in self.snapshots:
            utils.pend("copy snapshot: %s" % s)
            utils.execute(['cp', '%s.%s' % (self.path_raw, s), new.path])
            utils.ok()
        new.scan_snapshots()


    def list(self, **kwargs: dict):
        for name in os.listdir(utils.CHEFROOT_VM):
            if not os.path.isdir('%s/%s' % (utils.CHEFROOT_VM, name)):
                continue
            print(VM(name))

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
        pcreate.add_argument('name', help="Machine name")
        pcreate.add_argument('size', default='5120M', nargs='?',
                             help="VM size [default=5120M]")

        # delete
        pdelete = pcmd.add_parser('delete', help="Delete an existing VM")
        pdelete.set_defaults(action=VM.delete)
        pdelete.add_argument('name', help="Machine name")

        # export
        pexport = pcmd.add_parser('export',
                                  help="Export VM to a gzipped tarball")
        pexport.set_defaults(action=VM.export)
        pexport.add_argument('name', help="Machine name")
        pexport.add_argument('targz', default=None, nargs='?',
                             help="Tarball name [default=./<Machine name>.tar.gz]")

        # import
        pimport = pcmd.add_parser('import',
                                  help="Import a VM from a gzipped tarball")
        pimport.set_defaults(action=VM._import)
        pimport.add_argument('-f','--force', action='store_true', default=False,
                             help="Force import, even if VM already exists")
        pimport.add_argument('targz', help="Tarball name")
        pimport.add_argument('name', help="Machine name")

        # clone
        pclone = pcmd.add_parser('clone',
                                 help="Create an exact copy of an existing VM")
        pclone.set_defaults(action=VM.clone)
        pclone.add_argument('-f','--force', action='store_true', default=False,
                            help="Force cloning, even if VM already exists")
        pclone.add_argument('name', help="Machine name (original)")
        pclone.add_argument('clone', help="Machine name (clone)")

        # list
        plist = pcmd.add_parser('list', help="List VMs")
        plist.set_defaults(action=VM.list)

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
    vm = VM(name=args.get('name', None))
    exit(args['action'](vm, **args))
