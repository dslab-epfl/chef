import os
import sys
import grp
import subprocess


class ExecError(Exception):
    def __init__(self, msg):
        self.message = msg
    def __str__(self):
        return self.message


def execute(cmd:[str], stdin:str=None, stdout:bool=False, stderr:bool=False,
           msg:str=None, iowrap:bool=False):
    _in = subprocess.PIPE if stdin else None
    _out = None if stdout else subprocess.PIPE
    _err = None if stdout else subprocess.PIPE
    _indata = bytes(stdin, 'utf-8') if stdin else None
    sp = subprocess.Popen(cmd, stdin=_in, stdout=_out, stderr=_err)
    out, err = sp.communicate(input=_indata)
    if stdout:
        print(out.decode(), end='')
    if stderr:
        print(err.decode(), end='', file=sys.stderr)
    if sp.returncode != 0 and msg:
        print("Failed to %s: %s" % (msg, err.decode()), file=sys.stderr)
    if iowrap:
        return out.decode(), err.decode(), sp.returncode
    else:
        return sp.returncode


def sudo(cmd:[str], sudo_msg:str=None, stdin:str=None, stdout:bool=False,
         stderr:bool=False, msg:str=None, iowrap:bool=False):
    sudo_prompt = '(%s) [sudo] ' % (sudo_msg, cmd[0])[sudo_msg is None]
    sudo_cmd = ['sudo', '-p', sudo_prompt] + cmd
    return execute(sudo_cmd, stdin=stdin, stdout=stdout, stderr=stderr, msg=msg,
                   iowrap=iowrap)


def set_permissions(path: str):
    try:
        os.chown(path, -1, grp.getgrnam('kvm').gr_gid)
        os.chmod(path, 0o775 if os.path.isdir(path) else 0o664)

        # TODO: ACL
    except PermissionError:
        print("Cannot modify permissions for %s: Permission denied" % path,
              file=sys.stderr)
        exit(1)


def prompt_yes_no(msg: str, default: bool = None):
    yes = ['y', 'ye', 'yes']
    no = ['n', 'no']
    pmsg = '%s [%s/%s] ' % (msg, ('y', 'Y')[default == True],
                                 ('n', 'N')[default == False])
    while (True):
        try:
            user = input(pmsg).lower()
        except KeyboardInterrupt:
            print('\nAborting', file=sys.stderr)
            exit(1)
        if user in yes or user in no:
            break
        if user == '' and default != None:
            user = ('n', 'y')[default]
            break
    return user in yes


def prompt_int(msg: str, default: int = None):
    pmsg = '%s%s ' % (msg, ('', ' [default=%d]' % default)[default != None])
    while (True):
        try:
            user = input(pmsg)
        except KeyboardInterrupt:
            print('\nAborting', file=sys.stderr)
            exit(1)
        if user == '' and default != None:
            val = default
            break
        else:
            try:
                val = int(user)
                break
            except ValueError:
                print('Please enter an integer value', file=sys.stderr)
    return val
