#!/usr/bin/env python3

import os

RUNPATH = os.path.dirname(__file__)

class Chef:
    CHEF_ROOT = '/chef'
    DATA_ROOT = '/data'
    IMAGE_FILE = os.path.join(DATA_ROOT, 'chef_disk')
    IMAGE_FILE_RAW = '%s.raw' % IMAGE_FILE
    IMAGE_FILE_S2E = '%s.s2e' % IMAGE_FILE
    WATCHDOG_PORT = 4321

    DEFAULT_WATCHDOG_PORT = 1234
    DEFAULT_MONITOR_PORT = 12345
    DEFAULT_CHEF_ROOT_HOST = RUNPATH
    DEFAULT_DATA_ROOT_HOST = '/var/local/chef'
    DEFAULT_CHEF_CONFIG_FILE = os.path.join(DEFAULT_CHEF_ROOT, 'config')
    DEFAULT_DATA_VM_PATH = os.path.join(DEFAULT_DATA_ROOT, 'vm')
    DEFAULT_DATA_OUT_PATH = os.path.join(DEFAULT_DATA_ROOT, 'expdata')

    def __init__(self):
        # TODO
        pass

if __name__ == '__main__':
    chef = Chef()
