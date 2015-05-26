#!/usr/bin/env python3

from chef import Chef
from batch import Batch
from chefdocker import ChefDocker
import sys
import subprocess


class ChefDockerBatch:
    DEFAULT_WATCHDOG_PORT_BASE = 1234
    DEFAULT_MONITOR_PORT_BASE = 12345
    DEFAULT_VNC_DISPLAY_BASE = 0

    def __init__(self,
                 batch,
                 arch: str = Chef.DEFAULT_ARCH,
                 target: str = Chef.DEFAULT_TARGET,
                 mode: str = Chef.DEFAULT_MODE,
                 image_name: str = Chef.DEFAULT_IMAGE_NAME,
                 network: str = None,
                 config_file: str = None,
                 chef_root: str = Chef.DEFAULT_CHEF_ROOT,
                 data_root: str = Chef.DEFAULT_DATA_ROOT,
                 out_dir: str = None,
                 image_dir: str = None,
                 timeout: int = Chef.DEFAULT_TIMEOUT,
                 watchdog_port_base: int = DEFAULT_WATCHDOG_PORT_BASE,
                 monitor_port_base: int = DEFAULT_MONITOR_PORT_BASE,
                 vnc_display_base: int = DEFAULT_VNC_DISPLAY_BASE,
                 docker_image: str = ChefDocker.DEFAULT_IMAGE):
        self.batch = batch
        self.watchdog_port_base = watchdog_port_base
        self.monitor_port_base = monitor_port_base
        self.vnc_display_base = vnc_display_base
        self.dockers = []
        batch_cmds = batch.get_cmd_lines()
        for cmd, i in zip(batch_cmds, range(len(batch_cmds))):
            chef = Chef(arch=arch, target=target, mode=mode,
                        image_name=image_name, network=network,
                        watchdog_port=watchdog_port_base+i,
                        monitor_port=monitor_port_base+i,
                        vnc_display=vnc_display_base+i,
                        config_file=config_file, chef_root=chef_root,
                        data_root=data_root, out_dir=out_dir,
                        image_dir=image_dir, cores=1, run_mode='sym',
                        command=cmd, script=None, env={}, timeout=timeout,
                        load_id=i)
            docker = ChefDocker(chef, image=docker_image, privileged=False,
                                batch=True, watchdog_port=watchdog_port_base+i,
                                monitor_port=monitor_port_base+i,
                                vnc_display=vnc_display_base+i,
                                ports={}, shares={})
            self.dockers.append(docker)

    def get_cmd_lines(self):
        cmd_lines = []
        for docker in self.dockers:
            cmd_lines.append(docker.get_cmd_line())
        return cmd_lines


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: %s YAML [OUTDIR]" % sys.argv[0], file=sys.stderr)
        exit(1)

    path_yaml = sys.argv[1]
    path_results = 'chefdockerbatch_results' if len(sys.argv) < 3 else sys.argv[2]

    chef = Chef()
    batch = Batch(path_yaml, path_results)
    chefdockerbatch = ChefDockerBatch(batch)
    cmds = chefdockerbatch.get_cmd_lines()
    for c in cmds:
        print(' '.join(c))
