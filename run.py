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

"""Wrapper script for running S2E by following the RAW/S2E image manipulation workflow."""

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
import csv
import sys
import pipes
import time
import subprocess

from datetime import datetime, timedelta
from libccli.batch import Batch

THIS_DIR = os.path.dirname(__file__)

# Host:
HOST_CHEF_ROOT = os.path.abspath(THIS_DIR)
HOST_DATA_ROOT = os.path.join("/var", "local", "chef")

# Docker:
VERSION = "v0.6"
CHEF_ROOT = "/chef"
DATA_ROOT = "/data"
DATA_VM_DIR = os.path.join(DATA_ROOT, "vm")
DATA_OUT_DIR = os.path.join(DATA_ROOT, "expdata")
CHEF_CONFIG_DIR = os.path.join(CHEF_ROOT, "config")

# Qemu image:
RAW_IMAGE_PATH = os.path.join(DATA_VM_DIR, "chef_disk.raw")
S2E_IMAGE_PATH = os.path.join(DATA_VM_DIR, "chef_disk.s2e")

# Default configuration values:
DEFAULT_CONFIG_FILE = os.path.join(CHEF_CONFIG_DIR, "default-config.lua")
DEFAULT_OUTDIR = DATA_OUT_DIR
DEFAULT_HOST_DATA_ROOT = HOST_DATA_ROOT
DEFAULT_COMMAND_PORT = 1234
DEFAULT_MONITOR_PORT = 12345
DEFAULT_VNC_DISPLAY = 0
DEFAULT_TAP_INTERFACE = "tap0"
GDB_BIN = "gdb"
STRACE_BIN = "strace"
COMMAND_SEND_TIMEOUT = 60
MAX_DEFAULT_CORES = 4


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


class CommandError(Exception):
    pass


def get_default_ip():
    return 'dslab-worf.epfl.ch' #FIXME #TODO #XXX

    iface = "localhost"

    f = open('/proc/net/route')
    for i in csv.DictReader(f, delimiter="\t"):
        if i['Destination'] == 0:
            iface = i['Iface']
            break

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    return socket.inet_ntoa(
        fcntl.ioctl(
            s.fileno(),
            0x8915, # SIOCGIFADDR
            struct.pack('256s', iface)
        )[20:24]
    )


def send_command(command, host, port):
    conn = None
    try:
        conn = http.client.HTTPConnection(host, port=port, timeout=COMMAND_SEND_TIMEOUT)
        conn.request("POST", command.url_path, command.to_json())
        response = conn.getresponse()
        if response.status != http.client.OK:
            raise CommandError("Invalid HTTP response received: %d" % response.status)
    except (socket.error, ConnectionRefusedError, http.client.HTTPException) as e:
        raise CommandError(e)
    finally:
        if conn:
            conn.close()


def async_send_command(command, host, port, timeout=COMMAND_SEND_TIMEOUT):
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


def parse_cmd_line():
    parser = argparse.ArgumentParser(description="High-level interface to S2E.")

    host_env = parser.add_mutually_exclusive_group()
    host_env.add_argument("-b", "--batch", action="store_true", default=False,
                          help="Headless (no GUI) mode")
    host_env.add_argument("-x", "--x-forward", action="store_true", default=False,
                          help="Optimize for X forwarding")

    network = parser.add_mutually_exclusive_group()
    network.add_argument("-t", "--net-tap", action="store_true", default=False,
                         help="Use a tap interface for networking")
    network.add_argument("-n", "--net-none", action="store_true", default=False,
                         help="Disable networking")

    exe_env = parser.add_mutually_exclusive_group()
    exe_env.add_argument("--gdb", action="store_true", default=False,
                         help="Run under gdb")
    exe_env.add_argument("--strace", action="store_true", default=False,
                         help="Run under strace")

    parser.add_argument("--data-root", default=DEFAULT_HOST_DATA_ROOT,
                        help="location of data root")
    parser.add_argument("-p", "--command-port", type=int, default=DEFAULT_COMMAND_PORT,
                        help="The command port configured for port forwarding")
    parser.add_argument("-m", "--monitor-port", type=int, default=DEFAULT_MONITOR_PORT,
                        help="The port on which the qemu monitor can be accessed")
    parser.add_argument("-v", "--vnc-display", type=int, default=DEFAULT_VNC_DISPLAY,
                        help="Display on which VNC can be accessed")

    parser.add_argument("-d", "--debug", action="store_true", default=False,
                        help="Run in debug mode")
    parser.add_argument("-y", "--dry-run", action="store_true", default=False,
                        help="Only display the S2E command to be run, don't run anything.")

    modes = parser.add_subparsers(help="The S2E operation mode")
    modes.required = True
    modes.dest = 'operation mode'

    kvm_mode = modes.add_parser("kvm", help="KVM mode")
    kvm_mode.add_argument("--cores", type=int,
                          default=min(multiprocessing.cpu_count(), MAX_DEFAULT_CORES),
                          help="Number of virtual cores")
    kvm_mode.set_defaults(mode="kvm")

    prepare_mode = modes.add_parser("prep", help="Prepare mode")
    prepare_mode.add_argument("-l", "--load", action="store_true", default=False,
                              help="Load from snapshot 1")
    prepare_mode.set_defaults(mode="prep")

    symbolic_mode = modes.add_parser("sym", help="Symbolic mode")
    symbolic_mode.add_argument("-f", "--config", default=DEFAULT_CONFIG_FILE,
                               help="The S2E configuration file")
                               # XXX path is for docker container
    symbolic_mode.add_argument("-o", "--out-dir", default=DEFAULT_OUTDIR,
                               help="S2E output directory")
                               # XXX path is for docker container
    symbolic_mode.add_argument("-t", "--time-out", type=int,
                               help="Timeout (in seconds)")
    symbolic_mode.add_argument("--batch-file", type=str, default=None,
                               help="YAML file that contains the commands to be executed")
    symbolic_mode.add_argument("-e", "--env-var", action="append",
                               help="Environment variable for the command (can be used multiple times)")
    symbolic_mode.add_argument("--script", nargs=2,
                               help="Execute script given as a pair <test file, test name>")
    symbolic_mode.add_argument("command", nargs=argparse.REMAINDER,
                               help="The command to execute")
    symbolic_mode.set_defaults(mode="sym")

    return parser.parse_args()


def build_docker_cmd_line(args, command: [str]):
    docker_cmd_line = ["docker", "run", "--rm"]
    if not args.batch:
        docker_cmd_line.extend(["-t", "-i"])
    docker_cmd_line.extend(["-v", "%s:%s" % (HOST_CHEF_ROOT, CHEF_ROOT),
                     "-v", "%s:%s" % (args.data_root, DATA_ROOT),
                     "-p", "%d:%d" % (args.command_port, args.command_port),
                     "-p", "%d:%d" % (args.monitor_port, args.monitor_port),
                     "-p", "%d:%d" % (5900 + args.vnc_display, 5900 + args.vnc_display)
                     ])
    if args.mode == "kvm":
        docker_cmd_line.append("--privileged=true")
    docker_cmd_line.append("dslab/s2e-chef:%s" % VERSION)
    if args.mode == "kvm":
        docker_cmd_line.extend(["/bin/bash", "-c",
                                "sudo setfacl -m group:kvm:rw /dev/kvm; %s"
                                                             % ' '.join(command)
                               ])
    else:
        docker_cmd_line.extend(command)
    return docker_cmd_line


def build_qemu_cmd_line(args):
    # Qemu path
    qemu_path = os.path.join(CHEF_ROOT, "build",
                             "%s-%s-%s" % ("i386",
                                           ("release", "debug")[args.debug],
                                           "normal"),
                             "opt", "bin",
                             "qemu-system-i386%s" % ("", "-s2e")[args.mode == "sym"]
                            )

    # Base command
    qemu_cmd_line = []
    if args.gdb:
        qemu_cmd_line.extend([GDB_BIN, "--args"])
    if args.strace:
        qemu_cmd_line.extend([STRACE_BIN, "-e", "open"])
    qemu_cmd_line.append(qemu_path);
    qemu_cmd_line.append((S2E_IMAGE_PATH, RAW_IMAGE_PATH)[args.mode == "kvm"])

    # General: CPU and command monitor
    qemu_cmd_line.extend(["-cpu", "pentium",  # Non-Pentium instructions cause spurious concretizations
                          "-monitor", "tcp::%d,server,nowait" % (args.monitor_port)
                         ])

    # Specific: Network, VNC, keyboard, KVM
    if args.net_none:
        qemu_cmd_line.extend(["-net", "none"])  # No networking
    else:
        qemu_cmd_line.extend(["-net", "nic,model=pcnet"])  # The only network device supported by S2E, IIRC
        if args.net_tap:
            qemu_cmd_line.extend(["-net", "tap,ifname=%s" % DEFAULT_TAP_INTERFACE])
        else:
            qemu_cmd_line.extend(["-net", "user",
                                  "-redir", "tcp:%d::4321" % args.command_port  # Command port forwarding
                                 ])

    qemu_cmd_line.extend(["-vnc", ":%d" % (args.vnc_display)])

    if args.x_forward:
        qemu_cmd_line.extend(["-k", "en-us"]) # Without it, the default key mapping is messed up

    if args.mode == "kvm":
        qemu_cmd_line.extend(["-enable-kvm",
                              "-smp", str(args.cores)  # KVM mode is the only multi-core one
                             ])

    # Snapshots (assuming they are stored in slot 1 with "savevm 1")
    if (args.mode == "prep" and args.load) or args.mode == "sym":
        qemu_cmd_line.extend(["-loadvm", "1"])
    if args.mode == "sym":
        qemu_cmd_line.extend(["-s2e-config-file", args.config, "-s2e-verbose"])
        if args.out_dir:
            qemu_cmd_line.extend(["-s2e-output-dir", args.out_dir])

    return qemu_cmd_line


def build_parallel_cmd_line():
    return ['parallel', '--delay', '1']


def build_smtlib_dump_cmd_line(out_path: str):
    return ['echo', 'dummy SMT-Lib dump to %s' % out_path]


def execute(args, cmd_line):
    print("Service      | Port  | How to connect")
    print("-------------+-------+---------------------------------------------")
    print("Watchdog     | %5d |" % args.command_port)
    print("VNC          | %5d | $vncclient {%s,localhost}:%d"
          % (5900 + args.vnc_display, get_default_ip(), args.vnc_display))
    print("Qemu monitor | %5d | {nc,telnet} {%s,localhost} %d"
          % (args.monitor_port, get_default_ip(), args.monitor_port))
    print

    if args.dry_run:
        print(' '.join(cmd_line))
        return

    print("** Executing %s\n" % ' '.join(cmd_line), file=sys.stderr)

    environ = dict(os.environ)

    if args.mode == "sym":
        environ["LUA_PATH"] = ";".join([os.path.join(os.path.dirname(args.config), "?.lua"),
                                        environ.get("LUA_PATH", "")])
        print("** Setting LUA_PATH=%s" % environ["LUA_PATH"], file=sys.stderr)

        if args.time_out:
            kill_me_later(args.time_out)
        if args.script:
            module_file, test = args.script
            with open(module_file, "r") as f:
                code = f.read()
            async_send_command(Script(code=code, test=test), "localhost", args.command_port)
        elif args.command:
            async_send_command(Command.from_cmd_args(args.command, args.env_var or []),
                               "localhost", args.command_port)

    os.execvpe(cmd_line[0], cmd_line, environ)


def main():
    args = parse_cmd_line()

    # batch-execute multiple commands:
    if args.mode == 'sym' and args.batch_file:
        batch = Batch(args.batch_file)
        batch_commands = batch.get_commands()

        printable_cmd_lines = ''
        out_dirs = []

        batch_offset = 1
        for command in batch_commands:
            cmd_lines = command.get_cmd_lines()
            for c in cmd_lines:
                c_out_dir = os.path.join('%04d-%s' % (batch_offset, os.path.basename(c[0])))
                c_out_path = os.path.join(DATA_ROOT, args.out_dir, c_out_dir)
                out_dirs.append(c_out_path)
                run_cmd = ['%s' % sys.argv[0], '--batch']
                if args.dry_run:
                    run_cmd.extend(['--dry-run'])
                if args.debug:
                    run_cmd.extend(['--debug'])
                run_cmd.extend(['--data-root', args.data_root])
                run_cmd.extend(['--command-port', str(args.command_port + batch_offset)])
                run_cmd.extend(['--monitor-port', str(args.monitor_port + batch_offset)])
                run_cmd.extend(['--vnc-display', str(args.vnc_display + batch_offset)])
                run_cmd.extend(['sym'])
                run_cmd.extend(['--config', os.path.join(DATA_ROOT, command.config)])
                run_cmd.extend(['--out-dir', c_out_path])
                if args.time_out:
                    run_cmd.extend(['--time-out', str(args.time_out)])
                if args.env_var:
                    run_cmd.extend(['--env-var', args.env_var])
                run_cmd.extend(c)
                printable_cmd_lines += ' '.join(run_cmd) + '\n'
                batch_offset += 1

        p2 = subprocess.Popen(build_parallel_cmd_line(), shell=False,
                              stdin=subprocess.PIPE)
        p2.communicate(bytes(printable_cmd_lines, 'utf-8'))
        cmd_line = build_docker_cmd_line(args, build_smtlib_dump_cmd_line(out_dirs))
        os.execvp(cmd_line[0], cmd_line)
    else:
        cmd_line = build_docker_cmd_line(args, build_qemu_cmd_line(args))
        execute(args, cmd_line)


if __name__ == "__main__":
    main()
