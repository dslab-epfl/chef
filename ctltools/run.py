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
import os
import signal
import socket
import sys
import time
import psutil
import utils
from datetime import datetime
from vm import VM

from datetime import datetime, timedelta

# Default values:
COMMAND_PORT = 1234
MONITOR_PORT = 12345
VNC_DISPLAY = 0
VNC_PORT_BASE = 5900
TIMEOUT_CMD = 60
CONFIGFILE = '%s/config/default-config.lua' % utils.CHEFROOT_SRC
NETWORK_MODE = 'user'
TAP_INTERFACE = 'tap0'
KVM_MEMORY_MIN = 512 * utils.MEBI
KVM_MEMORY_MAX =   4 * utils.GIBI
HOST_MEMORY = psutil.virtual_memory().total
KVM_MEMORY = '%dM' % (min(KVM_MEMORY_MAX, max(HOST_MEMORY / 4, KVM_MEMORY_MIN))
                      / utils.MEBI)
KVM_CORES = os.cpu_count()

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


def send_command(command, host, port, timeout):
    conn = None
    try:
        conn = http.client.HTTPConnection(host, port=port, timeout=timeout)
        conn.request("POST", command.url_path, command.to_json())
        response = conn.getresponse()
        if response.status != http.client.OK:
            raise CommandError("Invalid HTTP response received: %d" % response.status)
    except (socket.error, ConnectionRefusedError, http.client.HTTPException) as e:
        raise CommandError(e)
    finally:
        if conn:
            conn.close()


def async_send_command(command, host, port, timeout):
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
                utils.pend("sending command %s to %s:%d" % (command.args, host, port))
                send_command(command, host, port, timeout)
                utils.ok("command %s successfully sent" % command.args)
            except CommandError as e:
                utils.pend(None, msg="%s, retrying for %d more seconds"
                                 % (e, (command_deadline - now).seconds))
            else:
                break
        else:
            utils.abort("command timeout")
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
                utils.info("execution timeout reached, interrupting")
                os.kill(pid, signal.SIGINT if not int_sent else 0)
                int_sent = True
            else:
                utils.info("execution timeout reached, killing")
                os.kill(pid, signal.SIGKILL)
                break
        except OSError:  # The process terminated
            break
    exit(0)


def execute(args, cmd_line):
    # Informative:
    ip = utils.get_default_ip()
    utils.info("Qemu monitor: port %d (connect with `{nc,telnet} %s %d)"
               % (args['monitor_port'], ip, args['monitor_port']))
    if args['headless']:
        utils.info("VNC: port %d (connect with `$vncclient %s:%d`)"
                   % (args['vnc_port'], ip, args['vnc_display']))
    if args['mode'] == 'sym':
        utils.info("Experiment name: %s" % args['expname'])
        if args['script'] or args['command']:
            utils.info("Watchdog: port %d" % args['command_port'])
    utils.debug("Command line:\n%s" % ' '.join(cmd_line))

    if args['dry_run']:
        exit(1)

    environ = dict(os.environ)

    if args['mode'] == 'sym':
        environ['LUA_PATH'] = ';'.join(['%s/?.lua' % args['config_root'],
                                        environ.get('LUA_PATH', '')])
        utils.debug("LUA_PATH=%s" % environ['LUA_PATH'])

        # each experiment gets its own directory:
        try:
            utils.pend("Creating experiment directory %s" % args['exppath'])
            os.makedirs(args['exppath'])
            utils.ok()
        except FileExistsError:
            utils.fail("Experiment %s already exists. Please choose another name."
                       % args['expname'])
            exit(1)

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
            async_send_command(obj, 'localhost', args['command_port'], TIMEOUT_CMD)

        # drop `s2e-last` symlink somewhere where it does not get in the way:
        os.chdir(utils.CHEFROOT_EXPDATA)

    os.execvpe(cmd_line[0], cmd_line, environ)


# PARSE COMMAND LINE ARGUMENTS =================================================

def parse_cmd_line():
    parser = argparse.ArgumentParser(description="High-level interface to running Chef.",
                                     prog=utils.INVOKENAME)

    # Chef build:
    parser.add_argument('-b', '--build', default=utils.BUILD,
                        help="Tuple (architecture:target:mode) describing the Chef build")

    # Qemu:
    parser.add_argument('-q', '--qemu-opt', type=str, action='append',
                        help="Additional arguments passed to qemu (may be passed multiple times)")
    parser.add_argument('-n', '--network', default='user', choices=['none','user','tap'],
                        help="Network mode [default=user]")
    parser.add_argument('-m','--memory', default=None,
                        help="Amount of main memory available to the VM")
    parser.add_argument('--monitor-port', type=int, default=MONITOR_PORT,
                        help="Port on which the qemu monitor is accessible")
    parser.add_argument('--vnc-display', type=int, default=VNC_DISPLAY,
                        help="VNC display number on which the VM is accessible")
    parser.add_argument('--headless', action='store_true', default=False,
                        help="Run qemu without graphical output")

    # Debug:
    exe_env = parser.add_mutually_exclusive_group()
    exe_env.add_argument('--gdb', action='store_true', default=False,
                         help="Run under gdb")
    exe_env.add_argument('--strace', action='store_true', default=False,
                         help="Run under strace")
    parser.add_argument('-y','--dry-run', action='store_true', default=False,
                        help="Only display the runtime configuration and exit")

    # Positional: VM name:
    parser.add_argument('VM[:snapshot]',
                        help="Name of the VM to use (see `ctl vm list`)")

    # Positional: Run mode:
    modes = parser.add_subparsers(help="The Chef operation mode")
    modes.required = True
    modes.dest = 'operation mode'

    #             Run mode: KVM:
    kvm_mode = modes.add_parser('kvm', help="KVM mode")
    kvm_mode.set_defaults(mode='kvm')
    kvm_mode.add_argument('-j', '--cores', type=int, default=KVM_CORES,
                          help="Number of virtual cores")

    #             Run mode: Preparation:
    prepare_mode = modes.add_parser('prep', help="Prepare mode")
    prepare_mode.set_defaults(mode='prep')

    #             Run mode: Symbolic:
    symbolic_mode = modes.add_parser('sym', help="Symbolic mode")
    symbolic_mode.set_defaults(mode='sym')
    symbolic_mode.add_argument('-f','--config-file', default=CONFIGFILE,
                               help="The Chef configuration file")
    symbolic_mode.add_argument('-t','--timeout', type=int, default=None,
                               help="Timeout (in seconds)")
    symbolic_mode.add_argument('-e','--env-var', action='append',
                               help="Environment variable for the command (can be used multiple times)")
    symbolic_mode.add_argument('--command-port', type=int, default=COMMAND_PORT,
                               help="Port on which the watchdog is accessible")
    symbolic_mode.add_argument('--script', nargs=2,
                               help="Execute script given as a pair <test file, test name>")
    symbolic_mode.add_argument('--batch-file', default=None,
                               help="YAML file that contains the commands to be executed")
    symbolic_mode.add_argument('--batch-delay', type=int, default=1,
                               help="Seconds to wait before executing next command")
    symbolic_mode.add_argument('--expname', default='auto_%s' % TIMESTAMP,
                               help="Name of the experiment")
    symbolic_mode.add_argument('command', nargs=argparse.REMAINDER,
                               help="The command to execute")

    # Parse arguments:
    args = parser.parse_args()
    kwargs = vars(args) # make it a dictionary, for easier use

    # Adapt a few options:
    vm_snapshot = kwargs['VM[:snapshot]'].split(':')
    kwargs['VM'] = vm_snapshot[0]
    kwargs['snapshot'] = vm_snapshot[1] if len(vm_snapshot) > 1 else None
    kwargs['vnc_port'] = VNC_PORT_BASE + kwargs['vnc_display']
    if not kwargs['memory']:
        kwargs['memory'] = ('128M', KVM_MEMORY)[kwargs['mode'] == 'kvm']
    if kwargs['mode'] == 'sym':
        kwargs['config_file'] = os.path.abspath(kwargs['config_file'])
        kwargs['exppath'] = os.path.join(utils.CHEFROOT_EXPDATA, kwargs['expname'])
        kwargs['config_root'], kwargs['config_filename'] = os.path.split(kwargs['config_file'])

    return kwargs


# ASSEMBLE COMMAND LINE ========================================================

def assemble_qemu_cmd_line(args):
    qemu_cmd_line = []

    # Debug:
    if args['gdb']:
        qemu_cmd_line.extend(['gdb', '--args'])
    if args['strace']:
        qemu_cmd_line.append('strace')

    # Qemu path:
    utils.parse_build(args['build'])
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

    # VM:
    vm = VM(args['VM'])
    if not os.path.exists(vm.path_raw):
        utils.fail('%s: VM does not exist' % vm.name)
        exit(1)

    # VM image:
    if args['mode'] == 'kvm':
        qemu_drive_options = 'if=virtio,format=raw'
    else:
        qemu_drive_options = 'cache=writeback,format=s2e'
    qemu_cmd_line.extend([
        '-drive',
        'file=%s,%s' % (vm.path_raw, qemu_drive_options)
    ])

    # Snapshots:
    if args['snapshot']:
        if args['snapshot'] not in vm.snapshots:
            utils.fail("%s: no such snapshot" % args['snapshot'])
        qemu_cmd_line.extend(['-loadvm', args['snapshot']])

    # General: CPU, memory, qemu monitor, VNC:
    qemu_cmd_line.extend([
        # Non-Pentium instructions cause spurious concretizations
        '-cpu', 'pentium',
        '-monitor', 'tcp::%d,server,nowait' % args['monitor_port'],
        '-m', args['memory'],
    ])
    if args['headless']:
        qemu_cmd_line.extend(['-vnc', ':%d' % args['vnc_display']])

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
            if args['mode'] == 'sym' and (args['script'] or args['command']):
                qemu_cmd_line.extend([
                    '-redir', 'tcp:%d::4321' % args['command_port']
                ])

    # Mode-specific: KVM:
    if args['mode'] == 'kvm':
        qemu_cmd_line.extend(['-enable-kvm', '-smp', str(args['cores'])])

    # Mode-specific: Symbolic:
    if args['mode'] == 'sym':
        qemu_cmd_line.extend([
            '-s2e-config-file', args['config_file'],
            '-s2e-verbose'
        ])
        qemu_cmd_line.extend(['-s2e-output-dir', args['exppath']])

    # User-defined qemu options:
    if args['qemu_opt']:
        qemu_cmd_line.extend(args['qemu_opt'])

    return qemu_cmd_line


def assemble_parallel_cmd_line(args: dict):
    return ['parallel', '-u', '--line-buffer', '--delay', '%d' % args['batch_delay']]


def assemble_cmd_line(args):
    cmd_line = assemble_qemu_cmd_line(args)
    return cmd_line


# MAIN =========================================================================

def batch_execute(args: dict):
    # get list of commands from batch file:
    bare_cmd_lines = []
    from batch import Batch
    batch = Batch(args['batch_file'])
    batch_commands = batch.get_commands() # the not-yet-expanded commands
    for command in batch_commands:
        bare_cmd_lines.extend(command.get_cmd_lines())

    # assemble command lines and experiment data output directories:
    cmd_lines = []
    batch_offset = 1
    for bare_cmd_line in bare_cmd_lines:
        # experiment data path:
        expname = '%s-%04d-%s' % (args['expname'],
                                  batch_offset,
                                  os.path.basename(bare_cmd_line[0]))

        # command:
        cmd_line = [sys.argv[0]] # recursively call ourselves:

        if args['dry_run']:
            cmd_line.extend(['--dry-run'])
        cmd_line.extend(['--monitor-port', '%d'
                        % (args['monitor_port'] + batch_offset)])
        cmd_line.extend(['--vnc-display', '%d'
                        % (args['vnc_display'] + batch_offset)])
        cmd_line.extend(['--build', utils.BUILD])
        cmd_line.extend(['--network', args['network']])
        cmd_line.extend(['--memory', args['memory']])
        cmd_line.extend([args['VM[:snapshot]']])
        cmd_line.extend(['sym'])
        cmd_line.extend(['--command-port', '%d'
                        % (args['command_port'] + batch_offset)])
        cmd_line.extend(['--expname', expname])
        cmd_line.extend(['--config-file', command.config])
        if args['timeout']:
            cmd_line.extend(['--timeout', '%d' % args['timeout']])
        if args['env_var']:
            cmd_line.extend(['--env-var', args['env_var']])
        cmd_line.extend([args['snapshot']])
        cmd_line.extend(bare_cmd_line)
        cmd_lines.append(' '.join(cmd_line))

        # counter:
        batch_offset += 1

    utils.execute(assemble_parallel_cmd_line(args), stdin='\n'.join(cmd_lines))


def main():
    args = parse_cmd_line()

    if args['mode'] == 'sym' and args['batch_file']:
        batch_execute(args)
    else:
        execute(args, assemble_cmd_line(args))


if __name__ == "__main__":
    main()
