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
import sys
import pipes
import time

from datetime import datetime, timedelta

THIS_DIR = os.path.dirname(__file__)

CHEF_DATA_ROOT = "/host"
CHEF_ROOT = os.path.abspath(THIS_DIR)

RAW_IMAGE_PATH = os.environ.get("CHEF_IMAGE_RAW",
                                os.path.join(CHEF_DATA_ROOT, "vm", "chef_disk.raw"))
S2E_IMAGE_PATH = os.environ.get("CHEF_IMAGE_S2E",
                                os.path.splitext(RAW_IMAGE_PATH)[0] + ".s2e")

DEFAULT_CONFIG_FILE = os.environ.get("CHEF_CONFIG",
                                     os.path.join(THIS_DIR, "config", "default-config.lua"))

DEFAULT_COMMAND_PORT = 1234
DEFAULT_TAP_INTERFACE = "tap0"
GDB_BIN = "gdb"
COMMAND_SEND_TIMEOUT = 60
MAX_DEFAULT_CORES = 4

DOCKER_S2E_PATH = "/host"

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


def send_command(command, host, port):
    conn = None
    try:
        conn = httplib.HTTPConnection(host, port=port, timeout=COMMAND_SEND_TIMEOUT)
        conn.request("POST", command.url_path, command.to_json())
        response = conn.getresponse()
        if response.status != httplib.OK:
            raise CommandError("Invalid HTTP response received: %d" % response.status)
    except (socket.error, httplib.HTTPException) as e:
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
                print >>sys.stderr, "** Could not send command (%s). Retrying for %d more seconds." % (
                    e, (command_deadline - now).seconds)
            else:
                break
        else:
            print >>sys.stderr, "** Command timeout. Aborting."
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

    parser.add_argument("-p", "--command-port", type=int, default=DEFAULT_COMMAND_PORT,
                        help="The command port configured for port forwarding")

    parser.add_argument("-d", "--debug", action="store_true", default=False,
                        help="Run in debug mode")
    parser.add_argument("--gdb", action="store_true", default=False,
                        help="Run under gdb")

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
    symbolic_mode.add_argument("-o", "--out-dir",
                               help="S2E output directory")
    symbolic_mode.add_argument("-t", "--time-out", type=int,
                               help="Timeout (in seconds)")
    symbolic_mode.add_argument("-e", "--env-var", action="append",
                               help="Environment variable for the command (can be used multiple times)")
    symbolic_mode.add_argument("--script", nargs=2,
                               help="Execute script given as a pair <test file, test name>")
    symbolic_mode.add_argument("command", nargs=argparse.REMAINDER,
                               help="The command to execute")
    symbolic_mode.set_defaults(mode="sym")

    return parser.parse_args()


def build_qemu_cmd_line(args):
    cmd_line = ["docker", "run", "--rm", "-t", "-i", "-p", "5900:5900",
                "-v", "%s:/host" % CHEF_ROOT,
                "dslab/s2e-chef:v0.3"]

    # Construct the qemu path
    qemu_path = os.path.join(CHEF_DATA_ROOT, "build",
                             "%s-%s-%s" % ("i386",
                                           ("release", "debug")[args.debug],
                                           "normal"),
                             "qemu",
                             ("i386-softmmu", "i386-s2e-softmmu")[args.mode == "sym"],
                             "qemu-system-i386")

    # Construct the command line
    if args.gdb:
        cmd_line.extend([GDB_BIN, "--args"])
    cmd_line.extend([qemu_path, (S2E_IMAGE_PATH, RAW_IMAGE_PATH)[args.mode == "kvm"],
                     "-cpu", "pentium"  # Non-Pentium instructions cause spurious concretizations
                     ])
    if args.net_none:
        cmd_line.extend(["-net", "none"])  # No networking
    else:
        cmd_line.extend(["-net", "nic,model=pcnet"])  # The only network device supported by S2E, IIRC
        if args.net_tap:
            cmd_line.extend(["-net", "tap,ifname=%s" % DEFAULT_TAP_INTERFACE])
        else:
            cmd_line.extend(["-net", "user",
                             "-redir", "tcp:%d::4321" % args.command_port  # Command port forwarding
                             ])

    if args.batch:
        cmd_line.extend(["-vnc", ":0",
                         "-monitor", "/dev/null"
                         ])
    elif args.x_forward:
        cmd_line.extend(["-k", "en-us",  # Without it, the default key mapping is messed up
                         "-monitor", "stdio"  # The monitor is not accessible over the regular Ctrl+Alt+2
                         ])

    if args.mode == "kvm":
        cmd_line.extend(["-enable-kvm",
                         "-smp", str(args.cores)  # KVM mode is the only multi-core one
                         ])
    if (args.mode == "prep" and args.load) or args.mode == "sym":
        cmd_line.extend(["-loadvm", "1"])  # Assumption: snapshot is saved in slot 1 ("savevm 1" in the monitor)
    if args.mode == "sym":
        cmd_line.extend(["-s2e-config-file", args.config,
                         "-s2e-verbose"])
        if args.out_dir:
            cmd_line.extend(["-s2e-output-dir", args.out_dir])

    return cmd_line


def main():
    args = parse_cmd_line()
    qemu_cmd_line = build_qemu_cmd_line(args)

    if args.dry_run:
        print(" ".join(pipes.quote(arg) for arg in qemu_cmd_line))
        return

    print("** Executing %s\n" % " ".join(pipes.quote(arg) for arg in qemu_cmd_line), file=sys.stderr)

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

    os.execvpe(qemu_cmd_line[0], qemu_cmd_line, environ)


if __name__ == "__main__":
    main()
