#!/usr/bin/env python3

import os
import sys
import subprocess

class Docker:
    DEFAULT_VERSION = 'v0.6'
    DEFAULT_IMAGE = 'dslab/s2e-chef:%s' % DEFAULT_VERSION
    DEFAULT_COMMAND = ['/bin/sh']

    def __init__(self,
                 image: str = DEFAULT_IMAGE,
                 command: [str] = DEFAULT_COMMAND,
                 privileged: bool = False,
                 batch: bool = False,
                 ports: {int: int} = {},
                 shares: {str: str} = {}):
        self.image = image
        self.command = command
        self.privileged = privileged
        self.batch = batch
        self.ports = ports
        self.shares = shares

    def get_cmd_line(self):
        cmd_line = ['docker', 'run', '--rm']
        if not self.batch:
            cmd_line.extend(['-t', '-i'])
        for g, h in self.shares.items():
            cmd_line.extend(['-v', '%s:%s' % (h, g)])
        for h, g in self.ports.items():
            cmd_line.extend(['-p', '%d:%d' % (h, g)])
        if self.privileged:
            cmd_line.append('--privileged=true')
        cmd_line.append(self.image)
        cmd_line.extend(self.command)
        return cmd_line


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: %s COMMAND ..." % sys.argv[0], file=sys.stderr)
        exit(1)
    cmd = sys.argv[1:]
    docker = Docker(command=cmd, privileged=False, batch=False)
    print(docker.get_cmd_line())
