#!/usr/bin/env python3

class Qemu:
    VNC_BASE = 5900
    DEFAULT_TAP_INTERFACE = 'tap0'
    DEFAULT_CORES = 1
    DEFAULT_ARCH = 'x86_64'
    DEFAULT_QEMU_PATH = '/usr/bin/qemu-system-%s' % DEFAULT_ARCH

    def __init__(self, qemu_path: str = DEFAULT_QEMU_PATH):
        self.qemu_path = qemu_path


    def get_cmd_line(self, image_path: str, ports: {int: int} = {},
                     network: str = None, vnc_display:int = None,
                     monitor_port: int = None, kvm:bool = False,
                     cores: int = DEFAULT_CORES, load_id: str = None,
                     args: [str] = []):
        cmd_line = [self.qemu_path, image_path]

        # CPU:
        if kvm:
            cmd_line.extend(['-enable-kvm',
                             '-cpu', 'host',
                             '-smp', str(self.cores)])
        else:
            # non-Pentium instructions cause spurious concretisations
            cmd_line.extend(['-cpu', 'pentium'])

        # network:
        if network is None or network == 'none':
            cmd_line.extend(['-net', 'none'])
        else:
            cmd_line.extend(['-net', 'nic,model=pcnet'])  # The only network device supported by S2E, IIRC
            if network == 'tap':
                cmd_line.extend(['-net', 'tap,ifname=%s' % DEFAULT_TAP_INTERFACE])
            else:
                netuser = 'user'
                for h, g in ports.items():
                    netuser += ',hostfwd=tcp::%d-:%d' % (h, g)
                cmd_line.extend(['-net', netuser])

        # VNC:
        if vnc_display is not None:
            cmd_line.extend(['-vnc', ':%d' % vnc_display])

        # Monitor:
        if monitor_port is not None:
            cmd_line.extend(['-monitor', 'tcp::%d,server,nowait' % monitor_port])

        # Load snapshot:
        if load_id is not None:
            cmd_line.extend(['-loadvm', load_id])

        # Custom arguments:
        cmd_line.extend(args)
        return cmd_line


if __name__ == '__main__':
    q = Qemu()
    cmd_line = q.get_cmd_line('/dev/null', ports={34:35}, network='user',
                              vnc_display=0, monitor_port=1251, kvm=False,
                              cores=2, load_id=None, args=[])
    print(cmd_line)
