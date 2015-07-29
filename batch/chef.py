#!/usr/bin/env python3

import os
from .qemu import Qemu

RUNPATH = os.path.dirname(__file__)


class ChefError(Exception):
    pass


class Chef:
    WATCHDOG_PORT = 4321

    DEFAULT_WATCHDOG_PORT = 1234
    DEFAULT_MONITOR_PORT = 12345
    DEFAULT_CHEF_ROOT = RUNPATH
    DEFAULT_DATA_ROOT = '/var/local/chef'
    DEFAULT_IMAGE_NAME = 'chef_disk'
    DEFAULT_ARCH = 'x86_64'    # i386, x86_64
    DEFAULT_TARGET = 'release' # release, debug
    DEFAULT_MODE = 'normal'    # normal, asan, libmemtracer
    DEFAULT_TIMEOUT = 60
    DEFAULT_RUN_MODE = 'prep'  # prep, sym


    def __init__(self,
                 arch: str = DEFAULT_ARCH,
                 target: str = DEFAULT_TARGET,
                 mode: str = DEFAULT_MODE,
                 image_name: str = DEFAULT_IMAGE_NAME,
                 network: str = None,
                 watchdog_port: int = DEFAULT_WATCHDOG_PORT,
                 monitor_port: int = DEFAULT_MONITOR_PORT,
                 vnc_display: int = None,
                 config_file: str = None,
                 chef_root: str = DEFAULT_CHEF_ROOT,
                 data_root: str = DEFAULT_DATA_ROOT,
                 out_dir: str = None,
                 image_dir: str = None,
                 cores: int = 1,
                 run_mode: str = DEFAULT_RUN_MODE,
                 command: str = None,
                 script: (str, str) = None,
                 env: {str: str} = {},
                 timeout: int = DEFAULT_TIMEOUT,
                 load_id: str = None):

        if run_mode not in ['prep', 'sym']:
            raise Exception('invalid run_mode: %s' % (run_mode))

        self.chef_root = chef_root
        self.data_root = data_root
        self.out_dir = (out_dir, os.path.join(data_root, 'expdata'))[out_dir is None]
        image_ext = {'prep': 'raw', 'sym': 's2e'}[run_mode]
        self.image_dir = (image_dir, os.path.join(data_root,'vm'))[image_dir is None]
        self.image_path = os.path.join(self.image_dir,
                                       '%s.%s' % (image_name, image_ext))

        buildname = '%s-%s-%s' % (arch, target, mode)
        bindir = '%s-%ssoftmmu' % (arch, 's2e-' if run_mode == 'sym' else '')
        binname = 'qemu-system-%s' % arch
        self.qemu_path = os.path.join(self.chef_root, 'build', buildname,
                                      'qemu', bindir, binname)

        self.network = network
        self.ports = {watchdog_port: Chef.WATCHDOG_PORT}
        self.watchdog_port = watchdog_port
        self.monitor_port = monitor_port
        self.vnc_display = vnc_display
        self.cores = cores
        self.run_mode = run_mode
        self.command = command
        self.script = script
        self.env = env
        self.timeout = timeout
        if config_file is None:
            config_file = os.path.join(chef_root, 'config','default-config.lua')
        self.config_file = config_file
        self.args = ['-s2e-verbose', '-s2e-config-file', config_file]
        if out_dir is not None:
            self.args += ['-s2e-output-dir', out_dir]
        self.load_id = load_id

        self.qemu = Qemu(image_path=self.image_path, qemu_path=self.qemu_path,
                         ports=self.ports, network=self.network,
                         monitor_port=self.monitor_port,
                         vnc_display=self.vnc_display, kvm=False,
                         cores=self.cores, load_id=self.load_id, args=self.args)


    def get_cmd_line(self):
        return self.qemu.get_cmd_line()


    def run(self):
        delayed_kill(self.timeout)
        if self.run_mode == 'sym':
            if self.command is not None:
                self.send_async_cmd(Chef.Command(self.command, self.env))
            elif self.script is not None:
                file, test = self.script
                with open(file, 'r') as f:
                    code = f.read()
                self.send_async_cmd(Script(code=code, test=test))
                pass
            else:
                raise ChefError("No command specified for symbolic mode")
        subprocess.call(self.get_cmd_line())


    def delayed_kill(timeout, extra_time=60):
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


    def send_cmd(self, cmd: [str]):
        conn = None
        try:
            conn = httplib.HTTPConnection("localhost", port=self.watchdog_port,
                                          timeout=self.timeout)
            conn.request("POST", command.url_path, command.to_json())
            response = conn.getresponse()
            if response.status != httplib.OK:
                raise CommandError("Invalid HTTP response received: %d" % response.status)
        except (socket.error, httplib.HTTPException) as e:
            raise CommandError(e)
        finally:
            if conn:
                conn.close()


    def send_async_cmd(self, cmd: [str]):
        pid = os.getpid()
        if os.fork() != 0:
            return
        # Avoid the pesky KeyboardInterrupts in the child
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        command_deadline = datetime.now() + timedelta(seconds=self.timeout)
        while True:
            time.sleep(1)
            try:
                os.kill(pid, 0)
            except OSError:
                break
            now = datetime.now()
            if now < command_deadline:
                try:
                    self.send_cmd(cmd)
                except CommandError:
                    print("Waiting (-%ds) to send `%s`"
                          % ((command_deadline - now).seconds, cmd))
                else:
                    break
            else:
                print >>sys.stderr, "** Command timeout. Aborting."
                break
        exit(0)


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


if __name__ == '__main__':
    chef = Chef(run_mode='sym')
    print(chef.get_cmd_line())
