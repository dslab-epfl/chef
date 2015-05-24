#!/usr/bin/env python3

import os
from qemu import Qemu

RUNPATH = os.path.dirname(__file__)

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
    DEFAULT_RUN_MODE = 'prep'  # prep, sym

    def __init__(self, arch: str = DEFAULT_ARCH, target: str = DEFAULT_TARGET,
                 mode: str = DEFAULT_MODE, run_mode: str = DEFAULT_RUN_MODE,
                 chef_root: str = DEFAULT_CHEF_ROOT):
        self.chef_root = chef_root
        self.run_mode = run_mode
        buildname = '%s-%s-%s' % (arch, target, mode)
        bindir = '%s-%ssoftmmu' % (arch, 's2e-' if run_mode == 'sym' else '')
        binname = 'qemu-system-%s' % arch
        qemu_path = os.path.join(chef_root, 'build', buildname, 'qemu', bindir,
                                 binname)
        self.vm = Qemu(qemu_path)

    def get_cmd_line(self,
                     image_name: str = DEFAULT_IMAGE_NAME,
                     network: str = None,
                     watchdog_port: int = DEFAULT_WATCHDOG_PORT,
                     monitor_port: int = DEFAULT_MONITOR_PORT,
                     vnc_display: int = None,
                     config_file: str = None,
                     data_root: str = DEFAULT_DATA_ROOT,
                     out_dir: str = None,
                     img_dir: str = None,
                     cores: int = 1,
                     load_id: str = None):
        image_ext = {'prep': 'raw', 'sym': 's2e'}[self.run_mode]
        image_dir = os.path.join(data_root, 'vm')
        image_path = os.path.join(image_dir, '%s.%s' % (image_name, image_ext))
        ports = {watchdog_port: Chef.WATCHDOG_PORT}
        if config_file is None:
            config_file = os.path.join(self.chef_root, 'config',
                                       'default-config.lua')
        args = ['-s2e-verbose', '-s2e-config-file', config_file]
        if out_dir is not None:
            args += ['-s2e-output-dir', out_dir]
        return self.vm.get_cmd_line(image_path=image_path, ports=ports,
                                    network=network, vnc_display=vnc_display,
                                    monitor_port=monitor_port, kvm=False,
                                    cores=cores, load_id=load_id, args=args)

if __name__ == '__main__':
    chef = Chef()
    print(chef.get_cmd_line())
