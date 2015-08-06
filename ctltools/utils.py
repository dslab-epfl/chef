import os
import sys
import grp
import subprocess
import signal
import requests
import netifaces
import csv
import socket
import stat

# EXECUTION ====================================================================

def execute(cmd:[str], stdin:str=None, stdout:bool=False, stderr:bool=False,
            msg:str=None, iowrap:bool=False, outfile:str=None, env:dict=None):
    interrupted = False
    environ = dict(os.environ)
    if env:
        environ.update(env)
    _indata = bytes(stdin, 'utf-8') if stdin else None
    _in = subprocess.PIPE if stdin else None
    _out = open(outfile, 'wb') if outfile else None if stdout else subprocess.PIPE
    _err = None if stdout else subprocess.PIPE
    try:
        sp = subprocess.Popen(cmd, stdin=_in, stdout=_out, stderr=_err,
                              bufsize=0, env=environ)
        out, err = sp.communicate(input=_indata)
        if outfile:
            _out.close()
        if sp.returncode != 0:
            errprefix = "could not %s" % (msg, cmd[0])[msg is None]
            errmsg = "" if err is None else ": %s" % err.decode()
            fail("%s%s" % (errprefix, errmsg))
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


def which(cmd:str):
    paths = os.environ.get('PATH', '')
    for path in paths.split(':'):
        fullpath = '%s/%s' % (path, cmd)
        if os.path.exists(fullpath):
            return fullpath
    return None


# S2E/CHEF =====================================================================

# Paths:
THIS_PATH = os.path.abspath(os.path.dirname(__file__))
CHEFROOT_SRC = os.path.dirname(THIS_PATH)
CHEFROOT = os.path.dirname(CHEFROOT_SRC)
CHEFROOT_VM = '%s/vm' % CHEFROOT
CHEFROOT_EXPDATA = '%s/expdata' % CHEFROOT
CHEFROOT_BUILD = '%s/build' % CHEFROOT

# Build configurations:
ARCHS    = ['i386', 'x86_64', 'arm']
TARGETS  = ['release', 'debug']
MODES    = ['normal', 'asan']
ARCH     = ARCHS[0]
TARGET   = TARGETS[0]
MODE     = MODES[0]
RELEASE  = '%s:%s:%s' % (ARCH, TARGET, MODE)

def parse_release(release: str=None):
    global ARCH, TARGET, MODE, RELEASE, ARCHS, TARGET, MODES
    RELEASE = release or RELEASE
    release_tuple = RELEASE.split(':')
    arch, target, mode = release_tuple + [''] * (3 - len(release_tuple))
    if len(release_tuple) > 3:
        warn("trailing tokens in tuple: %s" % ':'.join(release_tuple[3:]))
    ARCH = arch or ARCHS[0]
    TARGET = target or TARGETS[0]
    MODE = mode or MODES[0]
    if ARCH not in ARCHS:
        fail("unknown architecture: %s" % ARCH)
        exit(1)
    if TARGET not in TARGETS:
        fail("unknown target: %s" % TARGET)
        exit(1)
    if MODE not in MODES:
        fail("unknown mode: %s" % MODE)
        exit(1)


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

    try:
        r = requests.get(url, stream=True)
        if r.status_code != 200:
            fail("%s: %d" % (url, r.status_code))
            return -1
    except requests.exceptions.ConnectionError:
        fail("Connection refused")
        return -1

    with open(path, 'wb') as file:
        file_size = int(r.headers['Content-Length'])
        file_size_current = 0
        file_size_block = 8 * unit
        try:
            for block in r.iter_content(file_size_block):
                file.write(block)
                file_size_current += len(block)
                progress = '%d MiB / %d MiB (%3d%%)' \
                           % (file_size_current / unit,
                              file_size / unit,
                              file_size_current * 100 / file_size)
                pend(progress, pending=True)
        except KeyboardInterrupt:
            abort("keyboard interrupt")
            exit(127)

    ok(progress)
    set_msg_prefix(None)
    return 0


def get_default_ip():
    iface = None
    # XXX assuming machine is only accessible via default route
    with open('/proc/net/route') as f:
        for i in csv.DictReader(f, delimiter='\t'):
            if int(i['Destination']) == 0:
                iface = i['Iface']
                break
    if iface:
        iface_data = netifaces.ifaddresses(iface)
        # XXX assuming default route interface has only one address
        return iface_data[netifaces.AF_INET][0]['addr']
    else:
        return '???'

# VALUES =======================================================================

KIBI = 1024
MEBI = KIBI * KIBI
GIBI = KIBI * MEBI

VNC_PORT_BASE = 5900

# MESSAGES =====================================================================

if sys.stdout.isatty() and sys.stderr.isatty():
    ESC_ERROR = '\033[31m'
    ESC_SUCCESS = '\033[32m'
    ESC_WARNING = '\033[33m'
    ESC_MISC = '\033[34m'
    ESC_SPECIAL = '\033[35m'
    ESC_RESET = '\033[0m'
    ESC_ERASE = '\033[K'
    ESC_RETURN = '\r'
else:
    ESC_ERROR = ''
    ESC_SUCCESS = ''
    ESC_WARNING = ''
    ESC_MISC = ''
    ESC_SPECIAL = ''
    ESC_RESET = ''
    ESC_ERASE = ''
    ESC_RETURN = '\n'

WARN = '[%sWARN%s]' % (ESC_WARNING, ESC_RESET)
FAIL = '[%sFAIL%s]' % (ESC_ERROR, ESC_RESET)
_OK_ = '[%s OK %s]' % (ESC_SUCCESS, ESC_RESET)
SKIP = '[%sSKIP%s]' % (ESC_SUCCESS, ESC_RESET)
INFO = '[%sINFO%s]' % (ESC_MISC, ESC_RESET)
ALRT = '[%s !! %s]' % (ESC_SPECIAL, ESC_RESET)
ABRT = '[%sABORT%s]' % (ESC_ERROR, ESC_RESET)
DEBG = '[%sDEBUG%s]' % (ESC_SPECIAL, ESC_RESET)
PEND = '[ .. ]'
msg_prefix = None

def set_msg_prefix(prefix: str):
    global msg_prefix
    msg_prefix = prefix

def print_msg(status: str, msg: str, file=sys.stdout, eol: str='\n',
              erase_prefix: bool=True):
    global msg_prefix
    print("%s%s%s%s%s" % (ESC_ERASE,
                          ('%s ' % status, '')[status is None],
                          ('%s' % msg_prefix, '')[msg_prefix is None],
                          (': ', '')[msg_prefix is None or msg is None],
                          (msg, '')[msg is None]),
          file=file, end=eol)
    if erase_prefix:
        set_msg_prefix(None)

# start ...
def pend(prefix: str, msg: str=None, pending: bool=True):
    set_msg_prefix(prefix)
    print_msg(PEND, msg, eol=('\n', ESC_RETURN)[pending], erase_prefix=False)

# ... and end
def info(msg: str):
    print_msg(INFO, msg)
def skip(msg: str):
    print_msg(SKIP, msg)
def ok(msg: str=None):
    print_msg(_OK_, msg)
def fail(msg: str):
    print_msg(FAIL, msg, file=sys.stderr)
def warn(msg: str):
    print_msg(WARN, msg, file=sys.stderr)
def alert(msg: str):
    print_msg(ALRT, msg)
def abort(msg: str):
    print()
    print_msg(ABRT, msg)

def debug(msg: str):
    print_msg(DEBG, msg, file=sys.stderr, erase_prefix=False)
