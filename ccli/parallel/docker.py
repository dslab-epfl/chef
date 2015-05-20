#!/usr/bin/env python3

import os
import sys
import subprocess

class Docker:
    DEFAULT_IMAGE = 'dslab/s2e-chef'
    DEFAULT_VERSION = 'v0.6'

    # Default values:
    WATCHDOG_PORT = 1234
    MONITOR_PORT = 12345
    VNC_PORT = 5900
    HOST_CHEF_ROOT = RUNPATH
    HOST_DATA_ROOT = '/var/local/chef'
    CHEF_ROOT = '/chef'
    CHEF_CONFIG_DIR = os.path.join(CHEF_ROOT, 'config')
    DATA_ROOT = '/data'
    DATA_VM_DIR = os.path.join(DATA_ROOT, 'vm')
    DATA_OUT_DIR = os.path.join(DATA_ROOT, 'expdata')

    # Qemu image:
    RAW_IMAGE_PATH = os.path.join(DATA_VM_DIR, 'chef_disk.raw')
    S2E_IMAGE_PATH = os.path.join(DATA_VM_DIR, 'chef_disk.s2e')

    def __init__(self, image: str = '%s:%s' % (DEFAULT_IMAGE, DEFAULT_VERSION),
                 ports: {int: int} = {}, shares: {str: str} = {}):
        self.ports = ports
        self.shares = shares
        self.image = image

    def get_cmd_line(self, command: [str], privileged: bool = False,
                     batch: bool = False):
        cmd_line = ['docker', 'run', '--rm']
        if not batch:
            cmd_line.extend(['-t', '-i'])
        for g, h in self.shares.items():
            cmd_line.extend(['-v', '%s:%s' % (h, g)])
        for h, g in self.ports.items():
            cmd_line.extend(['-p', '%d:%d' % (h, g)])
        if privileged:
            cmd_line.append('--privileged=true')
        cmd_line.append(self.image)
        cmd_line.extend(command)
        return cmd_line


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: %s COMMAND ..." % sys.argv[0], file=sys.stderr)
        exit(1)
    cmd = sys.argv[1:]
    docker = Docker('%s:%s' % (Docker.DEFAULT_IMAGE, Docker.DEFAULT_VERSION))
    cmd_line = docker.get_cmd_line(command=cmd, privileged=False, batch=False)
    subprocess.call(cmd_line)
