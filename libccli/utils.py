import os
import sys
import grp


class ExecError(Exception):
    def __init__(self, msg):
        self.message = msg
    def __str__(self):
        return self.message


def execute(cmd: [str]):
    pid = os.fork()
    if pid == 0:
        os.execvp(cmd[0], cmd)
        raise ExecError('exec(%s) failed' % ' '.join(cmd))
    else:
        (pid, status) = os.waitpid(pid, 0)
        return status >> 8


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
