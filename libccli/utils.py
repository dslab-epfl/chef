import os
import sys
import grp
import subprocess
import signal
import requests

# EXECUTION ====================================================================

class ExecError(Exception):
    def __init__(self, msg):
        self.message = msg
    def __str__(self):
        return self.message


def execute(cmd:[str], stdin:str=None, stdout:bool=False, stderr:bool=False,
            msg:str=None, iowrap:bool=False):
    interrupted = False
    _indata = bytes(stdin, 'utf-8') if stdin else None
    _in = subprocess.PIPE if stdin else None
    _out = None if stdout else subprocess.PIPE
    _err = None if stdout else subprocess.PIPE
    sp = subprocess.Popen(cmd, stdin=_in, stdout=_out, stderr=_err, bufsize=0)
    old_signals = {}
    try:
        out, err = sp.communicate(input=_indata)
        if sp.returncode != 0 and msg:
            fail("could not %s: %s" % (msg, err.decode()))
    except KeyboardInterrupt as ki:
        try:
            sp.wait()
        except KeyboardInterrupt:
            abort("second keyboard interrupt")
        finally:
            raise ki
    if iowrap:
        return out.decode(), err.decode(), sp.returncode
    else:
        return sp.returncode


def sudo(cmd:[str], sudo_msg:str=None, **kwargs: dict):
    sudo_prompt = '\n(%s) [sudo] ' % (sudo_msg, cmd[0])[sudo_msg is None]
    sudo_cmd = ['sudo', '-p', sudo_prompt] + cmd
    return execute(sudo_cmd, **kwargs)

# S2E/CHEF =====================================================================

def set_permissions(path: str):
    try:
        os.chown(path, -1, grp.getgrnam('kvm').gr_gid)
        os.chmod(path, 0o775 if os.path.isdir(path) else 0o664)
    except PermissionError:
        fail("Cannot modify permissions for %s: Permission denied" % path)
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

    set_msg_prefix(msg if msg else url)
    pend(pending=True)

    if os.path.exists(path) and not overwrite:
        skip("%s already exists" % path)
        set_msg_prefix(None)
        return

    r = requests.get(url, stream=True)
    if r.status_code != 200:
        fail('%d' % r.status_code)
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

# VALUES =======================================================================

KIBI = 1024
MEBI = KIBI * KIBI
GIBI = KIBI * MEBI

# MESSAGES =====================================================================

ESC_ERROR = '\033[31m'
ESC_WARNING = '\033[33m'
ESC_SUCCESS = '\033[32m'
ESC_MISC = '\033[34m'
ESC_SPECIAL = '\033[35m'
ESC_RESET = '\033[0m'
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

def info(msg: str):
    print_msg(INFO, msg)

def skip(msg: str):
    print_msg(SKIP, msg)

def ok(msg: str=None):
    print_msg(_OK_, msg)

def fail(msg: str=None):
    print_msg(FAIL, msg, file=sys.stderr)

def warn(msg: str):
    print_msg(WARN, msg, file=sys.stderr)

def alert(msg: str):
    print_msg(ALRT, msg)

def abort(msg: str):
    print()
    print_msg(ABRT, msg)

def pend(msg: str=None, pending: bool=True):
    print_msg(PEND, msg, eol=('\n', '\r')[pending])
