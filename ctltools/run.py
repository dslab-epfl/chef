#!/usr/bin/env python3
#
# Copyright (C) 2015 EPFL.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""Wrapper script for running Chef by following the RAW/S2E image manipulation workflow."""

__author__ = "stefan.bucur@epfl.ch (Stefan Bucur)"


import argparse
import http.client
import json
import multiprocessing
import os
import signal
import socket
import fcntl
import struct
import sys
import pipes
import time
import subprocess
import shutil
import utils
from datetime import datetime
from vm import VM

from datetime import datetime, timedelta

# Default values:
COMMAND_PORT = 1234
MONITOR_PORT = 12345
VNC_DISPLAY = 0
VNC_PORT_BASE = 5900
TIMEOUT = 60
CONFIGFILE = '%s/config/default-config.lua' % utils.CHEFROOT_SRC
NETWORK_MODE = 'user'
TAP_INTERFACE = 'tap0'

TIMESTAMP = time.strftime('%Y-%m-%dT%H:%M:%S.'
                          + datetime.utcnow().strftime('%f')[:3]
                          + '%z')


# COMMUNICATION WITH CHEF ======================================================

class CommandError(Exception):
    pass


class Command(object):
    url_path = '/command'

    def __init__(self, args, environment):
        self.args = args
        self.environment = environment

    def to_json(self):
        return json.dumps({
            "args": list(self.args),
            "environment": dict(self.environment)
        })

    @classmethod
    def from_cmd_args(cls, command, environ):
        return Command(list(command),
                       dict(env_var.split("=", 1) for env_var in environ))


class Script(object):
    url_path = '/script'

    def __init__(self, code, test):
        self.code = code
        self.test = test

    def to_json(self):
        return json.dumps({
            "code": self.code,
            "test": self.test,
        })


def send_command(command, host, port):
    conn = None
    try:
        conn = http.client.HTTPConnection(host, port=port, timeout=TIMEOUT)
        conn.request("POST", command.url_path, command.to_json())
        response = conn.getresponse()
        if response.status != http.client.OK:
            raise CommandError("Invalid HTTP response received: %d" % response.status)
    except (socket.error, ConnectionRefusedError, http.client.HTTPException) as e:
        raise CommandError(e)
    finally:
        if conn:
            conn.close()


def async_send_command(command, host, port, timeout=TIMEOUT):
    pid = os.getpid()
    if os.fork() != 0:
        return
    # Avoid the pesky KeyboardInterrupts in the child
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    command_deadline = datetime.now() + timedelta(seconds=timeout)
    while True:
        time.sleep(1)
        try:
            os.kill(pid, 0)
        except OSError:
            break
        now = datetime.now()
        if now < command_deadline:
            try:
                send_command(command, host, port)
            except CommandError as e:
                print("** Could not send command (%s). Retrying for %d more seconds." % (
                    e, (command_deadline - now).seconds), file=sys.stderr)
            else:
                break
        else:
            print("** Command timeout. Aborting.", file=sys.stderr)
            break
    exit(0)


# EXECUTE ======================================================================

def kill_me_later(timeout, extra_time=60):
    pid = os.getpid()
    if os.fork() != 0:
        return
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    int_deadline = datetime.now() + timedelta(seconds=timeout)
    kill_deadline = int_deadline + timedelta(seconds=extra_time)
    int_sent = False
    while True:
        time.sleep(1)
        now = datetime.now()
        try:
            if now < int_deadline:
                os.kill(pid, 0)  # Just poll the process
            elif now < kill_deadline:
                os.kill(pid, signal.SIGINT if not int_sent else 0)
                int_sent = True
            else:
                os.kill(pid, signal.SIGKILL)
                break
        except OSError:  # The process terminated
            break
    exit(0)


def execute(args, cmd_line):
    # Informative:
    ip = utils.get_default_ip()
    utils.info("VNC: port %d (connect with `$vncclient %s:%d`"
               % (args['vnc_port'], ip, args['vnc_display']))
    utils.info("Qemu monitor: port %d (connect with `{nc,telnet} %s %d"
               % (args['monitor_port'], ip, args['monitor_port']))
    if args['mode'] == 'sym':
        utils.info("Watchdog: port %d" % args['command_port'])
    utils.debug("Command line:\n%s" % ' '.join(cmd_line))

    if args['dry_run']:
        exit(1)

    environ = dict(os.environ)

    if args['mode'] == 'sym':
        environ['LUA_PATH'] = ';'.join(['%s/?.lua' % args['config_root'],
                                        environ.get('LUA_PATH', '')])
        utils.debug("LUA_PATH=%s" % environ['LUA_PATH'])

        if args['timeout']:
            kill_me_later(args['timeout'])

        obj = None
        if args['script']:
            module_file, test = args['script']
            with open(module_file, 'r') as f:
                code = f.read()
            obj = Script(code=code, test=test)
        if args['command']:
            obj = Command.from_cmd_args(args['command'], args['env_var'] or [])
        if obj:
            async_send_command(obj, 'localhost', args['command_port'],
                               timeout=args['timeout'])

        # each experiment gets its own directory:
        utils.pend("creating experiment directory %s" % args['exppath'])
        os.makedirs(args['exppath'])
        utils.ok()

        # drop `s2e-last` symlink somewhere where it does not get in the way:
        os.chdir(utils.CHEFROOT_EXPDATA)

    os.execvpe(cmd_line[0], cmd_line, environ)


# PARSE COMMAND LINE ARGUMENTS =================================================

def parse_cmd_line():
    parser = argparse.ArgumentParser(description="High-level interface to running Chef.",
                                     prog=utils.INVOKENAME)

    # Chef release:
    parser.add_argument('-r', '--release', default=utils.RELEASE,
                        help="Tuple (architecture:target:mode) describing the Chef binary release")

    # Network:
    parser.add_argument('-n','--network', default='user', choices=['none','user','tap'],
                        help="Network mode [default=user]")

    # Debug:
    exe_env = parser.add_mutually_exclusive_group()
    exe_env.add_argument('--gdb', action='store_true', default=False,
                         help="Run under gdb")
    exe_env.add_argument('--strace', action='store_true', default=False,
                         help="Run under strace")

    # Communication:
    parser.add_argument('-b','--background', action='store_true', default=False,
                        help="Fork execution to background") # XXX internal option for batch execution
    parser.add_argument('-m','--monitor-port', type=int,
                        default=MONITOR_PORT,
                        help="Port on which the qemu monitor is accessible")
    parser.add_argument('-v','--vnc-display', type=int,
                        default=VNC_DISPLAY,
                        help="VNC display number on which the VM is accessible")

    # Dry run:
    parser.add_argument('-y','--dry-run', action='store_true', default=False,
                        help="Only display the runtime configuration and exit")

    # Positional: VM name:
    parser.add_argument('vm_name',
                        help="Name of the VM to use (see `ctl vm list`)")

    # Positional: Run mode:
    modes = parser.add_subparsers(help="The Chef operation mode")
    modes.required = True
    modes.dest = 'operation mode'

    #             Run mode: KVM:
    kvm_mode = modes.add_parser('kvm', help="KVM mode")
    kvm_mode.set_defaults(mode='kvm')
    kvm_mode.add_argument('-j', '--cores', type=int,
                          default=multiprocessing.cpu_count(),
                          help="Number of virtual cores")

    #             Run mode: Preparation:
    prepare_mode = modes.add_parser('prep', help="Prepare mode")
    prepare_mode.set_defaults(mode='prep')
    prepare_mode.add_argument('-s', '--snapshot', default=None,
                              help="Snapshot to load from")

    #             Run mode: Symbolic:
    symbolic_mode = modes.add_parser('sym', help="Symbolic mode")
    symbolic_mode.set_defaults(mode='sym')
    symbolic_mode.add_argument('-f','--config-file', default=CONFIGFILE,
                               help="The Chef configuration file")
    symbolic_mode.add_argument('-t','--timeout', type=int, default=TIMEOUT,
                               help="Timeout (in seconds)")
    symbolic_mode.add_argument('-p','--command-port', type=int, default=COMMAND_PORT,
                               help="Port on which the watchdog is accessible")
    symbolic_mode.add_argument('-e','--env-var', action='append',
                               help="Environment variable for the command (can be used multiple times)")
    symbolic_mode.add_argument('--script', nargs=2,
                               help="Execute script given as a pair <test file, test name>")
    symbolic_mode.add_argument('-b','--batch-file', default=None,
                               help="YAML file that contains the commands to be executed")
    symbolic_mode.add_argument('--batch-delay', type=int, default=1,
                               help="Seconds to wait before executing next command")
    symbolic_mode.add_argument('--expname', default='auto_%s' % TIMESTAMP,
                               help="Name of the experiment")
    symbolic_mode.add_argument('snapshot',
                               help="Snapshot to resume from")
    symbolic_mode.add_argument('command', nargs=argparse.REMAINDER,
                               help="The command to execute")

    # Parse arguments:
    args = parser.parse_args()
    kwargs = vars(args) # make it a dictionary, for easier use

    # Adapt a few options:
    kwargs['vnc_port'] = VNC_PORT_BASE + kwargs['vnc_display']
    if kwargs['mode'] == 'sym':
        kwargs['exppath'] = os.path.join(utils.CHEFROOT_EXPDATA, kwargs['expname'])
        kwargs['config_root'], kwargs['config_filename'] = os.path.split(kwargs['config_file'])

    return kwargs


# BUILD COMMAND LINE ===========================================================

def build_qemu_cmd_line(args):
    qemu_cmd_line = []

    # VM:
    vm = VM(args['vm_name'])
    if not vm.exists():
        utils.fail('%s: VM does not exist' % vm.name)
        exit(1)

    # Debug:
    if args['gdb']:
        qemu_cmd_line.extend(['gdb', '--args'])
    if args['strace']:
        qemu_cmd_line.append('strace')

    # Qemu path:
    utils.parse_release(args['release'])
    arch, target, mode = utils.ARCH, utils.TARGET, utils.MODE
    qemu_path = os.path.join(
        utils.CHEFROOT_BUILD,
        '%s-%s-%s' % (arch, target, mode),
        'qemu',
        '%s%s-softmmu' % (arch, ('', '-s2e')[args['mode'] == 'sym']),
        'qemu-system-%s' % arch
    )

    # Base command:
    qemu_cmd_line.append(qemu_path);

    # General: VM image, CPU, qemu monitor, VNC:
    qemu_cmd_line.extend([
        # Non-Pentium instructions cause spurious concretizations
        '-cpu', 'pentium',
        '-monitor', 'tcp::%d,server,nowait' % args['monitor_port'],
        '-vnc', ':%d' % (args['vnc_display'])
    ])

    # Network:
    if args['network'] == 'none':
        qemu_cmd_line.extend(['-net', 'none'])
    else:
        # The only network device supported by S2E, IIRC
        qemu_cmd_line.extend(['-net', 'nic,model=pcnet'])
        if args['network'] == 'tap':
            qemu_cmd_line.extend(['-net', 'tap,ifname=%s' % TAP_INTERFACE])
        else:
            qemu_cmd_line.extend(['-net', 'user'])
            if args['mode'] == 'sym':
                qemu_cmd_line.extend([
                    '-redir', 'tcp:%d::4321' % args['command_port']
                ])

    # Mode-specific: KVM:
    if args['mode'] == 'kvm':
        qemu_cmd_line.extend(['-enable-kvm', '-smp', str(args['cores'])])

    # Mode-specific: non-KVM:
    elif args['snapshot']:
        if args['snapshot'] not in vm.snapshots:
            utils.fail("%s: no such snapshot" % args['snapshot'])
        qemu_cmd_line.extend(['-loadvm', args['snapshot']])

    # Mode-specific: Symbolic:
    if args['mode'] == 'sym':
        qemu_cmd_line.extend([
            '-s2e-config-file', args['config_file'],
            '-s2e-verbose'
        ])
        qemu_cmd_line.extend(['-s2e-output-dir', args['exppath']])

    # VM path:
    qemu_cmd_line.append((vm.path_s2e, vm.path_raw)[args['mode'] == 'kvm'])

    return qemu_cmd_line


def build_parallel_cmd_line(args: dict):
    return ['parallel', '--delay', args['batch_delay']]


def build_cmd_line(args):
    cmd_line = build_qemu_cmd_line(args)
    return cmd_line


# MAIN =========================================================================

def batch_execute():
    # get list of commands from batch file:
    bare_cmd_lines = []
    from ctltools.batch import Batch
    batch = Batch(args['batch_file'])
    batch_commands = batch.get_commands()
    for command in batch_commands:
        bare_cmd_lines.extend(command.get_cmd_lines())

    # assemble command lines and experiment data output directories:
    cmd_lines = []
    expdata_paths = []
    batch_offset = 1
    for bare_cmd_line in bare_cmd_lines:
        # experiment data path:
        expdata_dir = '%04d-%s' % (batch_offset, os.path.basename(c[0]))
        expdata_path = os.path.join(args['exppath'], expdata_dir)
        expdata_paths.append(expdata_path)

        # command:
        cmd_line = sys.argv[0] # recursively call ourselves:
        cmd_line.extend(['--background'])
        cmd_line.extend(['--command-port', '%d'
                        % (args['command_port'] + batch_offset)])
        cmd_line.extend(['--monitor-port', '%d'
                        % (args['monitor_port'] + batch_offset)])
        cmd_line.extend(['--vnc-display', '%d'
                        % (args['vnc_display'] + batch_offset)])
        cmd_line.extend(['sym'])
        cmd_line.extend(['--config-file', command.config])
        if args['timeout']:
            cmd_line.extend(['--timeout', '%d' % args['timeout']])
        if args['env_var']:
            cmd_line.extend(['--env-var', args['env_var']])
        if args['dry_run']:
            cmd_line.extend(['--dry-run'])
        cmd_line.extend(bare_cmd_line)
        cmd_lines.append(' '.join(cmd_line))

        # counter:
        batch_offset += 1

    p2 = subprocess.Popen(build_parallel_cmd_line(args), stdin=subprocess.PIPE)
    p2.communicate(bytes('\n'.join(cmd_lines), 'utf-8'))


def main():
    args = parse_cmd_line()

    if args['mode'] == 'sym' and args['batch_file']:
        # FIXME this feature has not been tested in a loooooong time:
        utils.warn("this feature has not been thoroughly tested yet!")
        batch_execute(args)
    else:
        execute(args, build_cmd_line(args))


if __name__ == "__main__":
    main()
