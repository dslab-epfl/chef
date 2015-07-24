import os
import sys
import grp
import subprocess
import signal
import requests
import netifaces
import csv
import socket

# EXECUTION ====================================================================

class ExecError(Exception):
    def __init__(self, msg):
        self.message = msg
    def __str__(self):
        return self.message


def execute(cmd:[str], stdin:str=None, stdout:bool=False, stderr:bool=False,
            msg:str=None, iowrap:bool=False, outfile:str=None):
    interrupted = False
    _indata = bytes(stdin, 'utf-8') if stdin else None
    _in = subprocess.PIPE if stdin else None
    _out = open(outfile, 'wb') if outfile else None if stdout else subprocess.PIPE
    _err = None if stdout else subprocess.PIPE
    try:
        sp = subprocess.Popen(cmd, stdin=_in, stdout=_out, stderr=_err, bufsize=0)
        out, err = sp.communicate(input=_indata)
        if outfile:
            _out.close()
        if sp.returncode != 0 and msg:
            fail("could not %s: %s" % (msg, err.decode()))
    except FileNotFoundError:
        fail("unknown command: %s" % cmd[0])
        if iowrap:
            return None, None, 255
        else:
            return 255
    except KeyboardInterrupt as ki:
        abort("keyboard interrupt")
        try:
            sp.wait()
        except KeyboardInterrupt:
            abort("second keyboard interrupt")
        exit(127) # XXX does not allow cleanup
    if iowrap:
        return out.decode(), err.decode(), sp.returncode
    else:
        return sp.returncode


def sudo(cmd:[str], sudo_msg:str=None, **kwargs: dict):
    sudo_prompt = '\n(%s) [sudo] ' % (sudo_msg, cmd[0])[sudo_msg is None]
    sudo_cmd = ['sudo', '-p', sudo_prompt] + cmd
    return execute(sudo_cmd, **kwargs)

# S2E/CHEF =====================================================================

def set_permissions(path: str, docker_uid: int = 431):
    set_msg_prefix("set permissions")
    pend(path)
    try:
        os.chown(path, -1, grp.getgrnam('kvm').gr_gid)
        os.chmod(path, 0o775 if os.path.isdir(path) else 0o664)
        for mode in ['normal', 'default']:
            cmd = ['setfacl']
            if mode == 'default':
                cmd.append('-d')
            cmd.extend(['-m', 'user:%d:rwx' % docker_uid, path])
            if execute(cmd, "set %s ACL permissions for user %d"
                       % (mode, docker_uid)) != 0:
                exit(1)
    except PermissionError:
        fail("Cannot modify permissions for %s: Permission denied" % path)
        exit(1)
    ok(path)

# USER INTERACTION =============================================================

def ask(msg: str, default: bool = None):
    yes = ['y', 'ye', 'yes']
    no = ['n', 'no']
    pmsg = '%s [%s/%s] ' % (msg, ('y', 'Y')[default == True],
                                 ('n', 'N')[default == False])
    while (True):
        user = input(pmsg).lower()
        if user in yes or user in no:
            break
        if user == '' and default != None:
            user = ('n', 'y')[default]
            break
    return user in yes

# NETWORK ======================================================================

def fetch(url: str, path: str, msg: str=None, overwrite: bool=False,
          unit: int=None):
    global KIBI
    if not unit:
        unit = KIBI
    if os.path.isdir(path):
        path = '%s/%s' % (path, os.path.basename(url))

    set_msg_prefix(msg if msg else url)
    pend(pending=True)

    if os.path.exists(path) and not overwrite:
        skip("%s already downloaded" % path)
        set_msg_prefix(None)
        return

    r = requests.get(url, stream=True)
    if r.status_code != 200:
        fail("%s: %d" % (url, r.status_code))
        exit(1)

    with open(path, 'wb') as file:
        file_size = int(r.headers['Content-Length'])
        file_size_current = 0
        file_size_block = 8 * unit
        try:
            for block in r.iter_content(file_size_block):
                file.write(block)
                file_size_current += len(block)
                pend("%d MiB / %d MiB (%3d%%)"
                     % (file_size_current / unit,
                        file_size / unit,
                        file_size_current * 100 / file_size),
                     pending=True)
        except KeyboardInterrupt:
            abort("keyboard interrupt")
            exit(127)

    ok()
    set_msg_prefix(None)


def get_default_ip():
    iface = None
    with open('/proc/net/route') as f:
        for i in csv.DictReader(f, delimiter='\t'):
            if i['Destination'] == 0:
                iface = i['Iface']
                break
    if iface:
        iface_data = netifaces.ifaddresses(iface)
        # FIXME assuming default route interface has only one address:
        return iface_data[netifaces.AF_INET][0]['addr']
    else:
        return '???'

# VALUES =======================================================================

KIBI = 1024
MEBI = KIBI * KIBI
GIBI = KIBI * MEBI

# MESSAGES =====================================================================

if sys.stdout.isatty() and sys.stderr.isatty():
    ESC_ERROR = '\033[31m'
    ESC_SUCCESS = '\033[32m'
    ESC_WARNING = '\033[33m'
    ESC_MISC = '\033[34m'
    ESC_SPECIAL = '\033[35m'
    ESC_RESET = '\033[0m'
else:
    ESC_ERROR = ''
    ESC_SUCCESS = ''
    ESC_WARNING = ''
    ESC_MISC = ''
    ESC_SPECIAL = ''
    ESC_RESET = ''

WARN = '[%sWARN%s]' % (ESC_WARNING, ESC_RESET)
FAIL = '[%sFAIL%s]' % (ESC_ERROR, ESC_RESET)
_OK_ = '[%s OK %s]' % (ESC_SUCCESS, ESC_RESET)
SKIP = '[%sSKIP%s]' % (ESC_SUCCESS, ESC_RESET)
INFO = '[%sINFO%s]' % (ESC_MISC, ESC_RESET)
ALRT = '[%s !! %s]' % (ESC_SPECIAL, ESC_RESET)
ABRT = '[%sABORT%s]' % (ESC_ERROR, ESC_RESET)
PEND = '[ .. ]'
msg_prefix = None

def set_msg_prefix(prefix: str):
    global msg_prefix
    msg_prefix = prefix

def print_msg(status: str, msg: str, file = sys.stdout, eol = '\n'):
    global msg_prefix
    print("%s%s%s%s" % (('%s ' % status, '')[status is None],
                      ('%s' % msg_prefix, '')[msg_prefix is None],
                      (': ', '')[msg_prefix is None or msg is None],
                      (msg, '')[msg is None]),
          file=file, end=eol)

def info(msg: str, eol: str='\n'):
    print_msg(INFO, msg, eol=eol)

def skip(msg: str, eol: str='\n'):
    print_msg(SKIP, msg, eol=eol)

def ok(msg: str=None, eol: str='\n'):
    print_msg(_OK_, msg, eol=eol)

def fail(msg: str=None, eol: str='\n'):
    print_msg(FAIL, msg, eol=eol, file=sys.stderr)

def warn(msg: str, eol: str='\n'):
    print_msg(WARN, msg, eol=eol, file=sys.stderr)

def alert(msg: str, eol: str='\n'):
    print_msg(ALRT, msg, eol=eol)

def abort(msg: str, eol: str='\n'):
    print()
    print_msg(ABRT, msg, eol=eol)

def pend(msg: str=None, pending: bool=True):
    print_msg(PEND, msg, eol=('\n', '\r')[pending])
