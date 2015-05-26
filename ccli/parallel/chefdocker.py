#!/usr/bin/env python3

from chef import Chef
from docker import Docker

class ChefDocker:
    DEFAULT_IMAGE = Docker.DEFAULT_IMAGE

    def __init__(self,
                 chef,
                 image: str = DEFAULT_IMAGE,
                 privileged: bool = False,
                 batch: bool = False,
                 watchdog_port: int = None,
                 monitor_port: int = None,
                 vnc_display: int = None,
                 ports: {int: int} = {},
                 shares: {str: str} = {}):
        self.image = image
        self.privileged = privileged
        self.batch = batch

        self.ports = {}
        self.watchdog_port = (watchdog_port, chef.watchdog_port)[watchdog_port is None]
        if self.watchdog_port is not None:
            self.ports[self.watchdog_port] = chef.watchdog_port
        self.monitor_port = (monitor_port, chef.monitor_port)[monitor_port is None]
        if self.monitor_port is not None:
            self.ports[self.monitor_port] = chef.monitor_port
        self.vnc_display = (vnc_display, chef.vnc_display)[vnc_display is None]
        if self.vnc_display is not None:
            VNC_BASE = 5900
            self.ports[VNC_BASE+self.vnc_display] = VNC_BASE+chef.vnc_display

        self.shares = {chef.chef_root: chef.chef_root,
                       chef.data_root: chef.data_root,
                       chef.out_dir: chef.out_dir,
                       chef.image_dir: chef.image_dir}

        self.docker = Docker(command=chef.get_cmd_line(), image=self.image,
                             privileged=self.privileged, batch=self.batch,
                             ports=self.ports, shares=self.shares)

    def get_cmd_line(self):
        return self.docker.get_cmd_line()


if __name__ == '__main__':
    chef = Chef()
    #print(chef.get_cmd_line())
    chefdocker = ChefDocker(chef)
    print(chefdocker.get_cmd_line())
