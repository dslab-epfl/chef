#!/usr/bin/env python3

class Qemu:
    DEFAULT_TAP_INTERFACE = 'tap0'
    DEFAULT_CORES = 1
    DEFAULT_ARCH = 'x86_64'
    DEFAULT_QEMU_PATH = '/usr/bin/qemu-system-%s' % DEFAULT_ARCH

    def __init__(self,
                 image_path: str,
                 qemu_path: str = DEFAULT_QEMU_PATH,
                 network: str = None,
                 ports: {int: int} = {},
                 monitor_port: int = None,
                 vnc_display: int = None,
                 kvm : bool = False,
                 cores: int = DEFAULT_CORES,
                 load_id: str = None,
                 args: [str] = []):
        if network not in [None, 'none', 'tap', 'user']:
            raise Exception('invalid network mode: %s' % network)
        self.qemu_path = qemu_path
        self.image_path = image_path
        self.network = network
        self.ports = ports
        self.monitor_port = monitor_port
        self.vnc_display = vnc_display
        self.kvm = kvm
        self.cores = cores
        self.load_id = load_id
        self.args = args

    def get_cmd_line(self):
        cmd_line = [self.qemu_path, self.image_path]

        # CPU:
        if self.kvm:
            cmd_line.extend(['-enable-kvm',
                             '-cpu', 'host',
                             '-smp', str(self.cores)])
        else:
            cmd_line.extend(['-cpu', 'pentium'])
            # (non-Pentium instructions cause spurious concretisations)
        # network:
        if self.network is None or self.network == 'none':
            cmd_line.extend(['-net', 'none'])
        else:
            cmd_line.extend(['-net', 'nic,model=pcnet'])
            # (the only network device supported by S2E, IIRC)
            if self.network == 'tap':
                cmd_line.extend(['-net',
                                 'tap,ifname=%s' % DEFAULT_TAP_INTERFACE])
            else:
                netuser = 'user'
                for h, g in self.ports.items():
                    netuser += ',hostfwd=tcp::%d-:%d' % (h, g)
                cmd_line.extend(['-net', netuser])
        # VNC:
        if self.vnc_display is not None:
            cmd_line.extend(['-vnc', ':%d' % self.vnc_display])
        # Monitor:
        if self.monitor_port is not None:
            cmd_line.extend(['-monitor',
                             'tcp::%d,server,nowait' % self.monitor_port])
        # Snapshot:
        if self.load_id is not None:
            cmd_line.extend(['-loadvm', str(self.load_id)])
        # Custom arguments:
        cmd_line.extend(self.args)
        return cmd_line


if __name__ == '__main__':
    qemu = Qemu('/dev/null', ports={34:35}, network='user', vnc_display=0,
                monitor_port=1251, kvm=False, cores=2, load_id=None, args=[])
    print(qemu.get_cmd_line())
