#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
import sys
import pwd
import time
import fcntl
import shutil
import logging
import socket
from threading import Lock, Thread as baseThread
from errno import EACCES, EAGAIN, EPIPE, ENOTCONN, ECONNABORTED
from errno import EINTR, ENOENT, EPERM, ESTALE, errorcode
from signal import signal, SIGTERM
import select as oselect
from os import waitpid as owaitpid

from conf import GLUSTERFS_LIBEXECDIR, UUID_FILE
sys.path.insert(1, GLUSTERFS_LIBEXECDIR)
EVENTS_ENABLED = True
try:
    from events.eventtypes import GEOREP_FAULTY as EVENT_GEOREP_FAULTY
    from events.eventtypes import GEOREP_ACTIVE as EVENT_GEOREP_ACTIVE
    from events.eventtypes import GEOREP_PASSIVE as EVENT_GEOREP_PASSIVE
    from events.eventtypes import GEOREP_CHECKPOINT_COMPLETED \
        as EVENT_GEOREP_CHECKPOINT_COMPLETED
except ImportError:
    # Events APIs not installed, dummy eventtypes with None
    EVENTS_ENABLED = False
    EVENT_GEOREP_FAULTY = None
    EVENT_GEOREP_ACTIVE = None
    EVENT_GEOREP_PASSIVE = None
    EVENT_GEOREP_CHECKPOINT_COMPLETED = None

try:
    from cPickle import PickleError
except ImportError:
    # py 3
    from pickle import PickleError

from gconf import gconf

try:
    # py 3
    from urllib import parse as urllib
except ImportError:
    import urllib

try:
    from hashlib import md5 as md5
except ImportError:
    # py 2.4
    from md5 import new as md5

# auxiliary gfid based access prefix
_CL_AUX_GFID_PFX = ".gfid/"
GF_OP_RETRIES = 10

CHANGELOG_AGENT_SERVER_VERSION = 1.0
CHANGELOG_AGENT_CLIENT_VERSION = 1.0
NodeID = None


def escape(s):
    """the chosen flavor of string escaping, used all over
       to turn whatever data to creatable representation"""
    return urllib.quote_plus(s)


def unescape(s):
    """inverse of .escape"""
    return urllib.unquote_plus(s)


def norm(s):
    if s:
        return s.replace('-', '_')


def update_file(path, updater, merger=lambda f: True):
    """update a file in a transaction-like manner"""

    fr = fw = None
    try:
        fd = os.open(path, os.O_CREAT | os.O_RDWR)
        try:
            fr = os.fdopen(fd, 'r+b')
        except:
            os.close(fd)
            raise
        fcntl.lockf(fr, fcntl.LOCK_EX)
        if not merger(fr):
            return

        tmpp = path + '.tmp.' + str(os.getpid())
        fd = os.open(tmpp, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        try:
            fw = os.fdopen(fd, 'wb', 0)
        except:
            os.close(fd)
            raise
        updater(fw)
        os.fsync(fd)
        os.rename(tmpp, path)
    finally:
        for fx in (fr, fw):
            if fx:
                fx.close()


def create_manifest(fname, content):
    """
    Create manifest file for SSH Control Path
    """
    fd = None
    try:
        fd = os.open(fname, os.O_CREAT | os.O_RDWR)
        try:
            os.write(fd, content)
        except:
            os.close(fd)
            raise
    finally:
        if fd is not None:
            os.close(fd)


def setup_ssh_ctl(ctld, remote_addr, resource_url):
    """
    Setup GConf ssh control path parameters
    """
    gconf.ssh_ctl_dir = ctld
    content = "SLAVE_HOST=%s\nSLAVE_RESOURCE_URL=%s" % (remote_addr,
                                                        resource_url)
    content_md5 = md5hex(content)
    fname = os.path.join(gconf.ssh_ctl_dir,
                         "%s.mft" % content_md5)

    create_manifest(fname, content)
    ssh_ctl_path = os.path.join(gconf.ssh_ctl_dir,
                                "%s.sock" % content_md5)
    gconf.ssh_ctl_args = ["-oControlMaster=auto", "-S", ssh_ctl_path]


def grabfile(fname, content=None):
    """open @fname + contest for its fcntl lock

    @content: if given, set the file content to it
    """
    # damn those messy open() mode codes
    fd = os.open(fname, os.O_CREAT | os.O_RDWR)
    f = os.fdopen(fd, 'r+b', 0)
    try:
        fcntl.lockf(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except:
        ex = sys.exc_info()[1]
        f.close()
        if isinstance(ex, IOError) and ex.errno in (EACCES, EAGAIN):
            # cannot grab, it's taken
            return
        raise
    if content:
        try:
            f.truncate()
            f.write(content)
        except:
            f.close()
            raise
    gconf.permanent_handles.append(f)
    return f


def grabpidfile(fname=None, setpid=True):
    """.grabfile customization for pid files"""
    if not fname:
        fname = gconf.pid_file
    content = None
    if setpid:
        content = str(os.getpid()) + '\n'
    return grabfile(fname, content=content)

final_lock = Lock()


def finalize(*a, **kw):
    """all those messy final steps we go trough upon termination

    Do away with pidfile, ssh control dir and logging.
    """
    final_lock.acquire()
    if getattr(gconf, 'pid_file', None):
        rm_pidf = gconf.pid_file_owned
        if gconf.cpid:
            # exit path from parent branch of daemonization
            rm_pidf = False
            while True:
                f = grabpidfile(setpid=False)
                if not f:
                    # child has already taken over pidfile
                    break
                if os.waitpid(gconf.cpid, os.WNOHANG)[0] == gconf.cpid:
                    # child has terminated
                    rm_pidf = True
                    break
                time.sleep(0.1)
        if rm_pidf:
            try:
                os.unlink(gconf.pid_file)
            except:
                ex = sys.exc_info()[1]
                if ex.errno == ENOENT:
                    pass
                else:
                    raise
    if gconf.ssh_ctl_dir and not gconf.cpid:
        def handle_rm_error(func, path, exc_info):
            if exc_info[1].errno == ENOENT:
                return
            raise exc_info[1]

        shutil.rmtree(gconf.ssh_ctl_dir, onerror=handle_rm_error)
    if getattr(gconf, 'state_socket', None):
        try:
            os.unlink(gconf.state_socket)
        except:
            if sys.exc_info()[0] == OSError:
                pass

    if gconf.log_exit:
        logging.info("exiting.")
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(kw.get('exval', 0))


def log_raise_exception(excont):
    """top-level exception handler

    Try to some fancy things to cover up we face with an error.
    Translate some weird sounding but well understood exceptions
    into human-friendly lingo
    """
    is_filelog = False
    for h in logging.getLogger().handlers:
        fno = getattr(getattr(h, 'stream', None), 'fileno', None)
        if fno and not os.isatty(fno()):
            is_filelog = True

    exc = sys.exc_info()[1]
    if isinstance(exc, SystemExit):
        excont.exval = exc.code or 0
        raise
    else:
        logtag = None
        if isinstance(exc, GsyncdError):
            if is_filelog:
                logging.error(exc.args[0])
            sys.stderr.write('failure: ' + exc.args[0] + '\n')
        elif isinstance(exc, PickleError) or isinstance(exc, EOFError) or \
            ((isinstance(exc, OSError) or isinstance(exc, IOError)) and
             exc.errno == EPIPE):
            logging.error('connection to peer is broken')
            if hasattr(gconf, 'transport'):
                gconf.transport.wait()
                if gconf.transport.returncode == 127:
                    logging.error("getting \"No such file or directory\""
                                  "errors is most likely due to "
                                  "MISCONFIGURATION, please remove all "
                                  "the public keys added by geo-replication "
                                  "from authorized_keys file in slave nodes "
                                  "and run Geo-replication create "
                                  "command again.")
                    logging.error("If `gsec_create container` was used, then "
                                  "run `gluster volume geo-replication "
                                  "<MASTERVOL> [<SLAVEUSER>@]<SLAVEHOST>::"
                                  "<SLAVEVOL> config remote-gsyncd "
                                  "<GSYNCD_PATH> (Example GSYNCD_PATH: "
                                  "`/usr/libexec/glusterfs/gsyncd`)")
                gconf.transport.terminate_geterr()
        elif isinstance(exc, OSError) and exc.errno in (ENOTCONN,
                                                        ECONNABORTED):
            logging.error('glusterfs session went down [%s]',
                          errorcode[exc.errno])
        else:
            logtag = "FAIL"
        if not logtag and logging.getLogger().isEnabledFor(logging.DEBUG):
            logtag = "FULL EXCEPTION TRACE"
        if logtag:
            logging.exception(logtag + ": ")
            sys.stderr.write("failed with %s.\n" % type(exc).__name__)
        excont.exval = 1
        sys.exit(excont.exval)


class FreeObject(object):

    """wildcard class for which any attribute can be set"""

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Thread(baseThread):

    """thread class flavor for gsyncd

    - always a daemon thread
    - force exit for whole program if thread
      function coughs up an exception
    """

    def __init__(self, *a, **kw):
        tf = kw.get('target')
        if tf:
            def twrap(*aa):
                excont = FreeObject(exval=0)
                try:
                    tf(*aa)
                except:
                    try:
                        log_raise_exception(excont)
                    finally:
                        finalize(exval=excont.exval)
            kw['target'] = twrap
        baseThread.__init__(self, *a, **kw)
        self.setDaemon(True)


class GsyncdError(Exception):
    pass


def getusername(uid=None):
    if uid is None:
        uid = os.geteuid()
    return pwd.getpwuid(uid).pw_name


def privileged():
    return os.geteuid() == 0


def boolify(s):
    """
    Generic string to boolean converter

    return
    - Quick return if string 's' is of type bool
    - True if it's in true_list
    - False if it's in false_list
    - Warn if it's not present in either and return False
    """
    true_list = ['true', 'yes', '1', 'on']
    false_list = ['false', 'no', '0', 'off']

    if isinstance(s, bool):
        return s

    rv = False
    lstr = s.lower()
    if lstr in true_list:
        rv = True
    elif not lstr in false_list:
        logging.warn("Unknown string (%s) in string to boolean conversion "
                     "defaulting to False\n" % (s))

    return rv


def eintr_wrap(func, exc, *a):
    """
    wrapper around syscalls resilient to interrupt caused
    by signals
    """
    while True:
        try:
            return func(*a)
        except exc:
            ex = sys.exc_info()[1]
            if not ex.args[0] == EINTR:
                raise


def select(*a):
    return eintr_wrap(oselect.select, oselect.error, *a)


def waitpid(*a):
    return eintr_wrap(owaitpid, OSError, *a)


def set_term_handler(hook=lambda *a: finalize(*a, **{'exval': 1})):
    signal(SIGTERM, hook)


def get_node_uuid():
    global NodeID
    if NodeID is not None:
        return NodeID

    NodeID = ""
    with open(UUID_FILE) as f:
        for line in f:
            if line.startswith("UUID="):
                NodeID = line.strip().split("=")[-1]
                break

    if NodeID == "":
        raise GsyncdError("Failed to get Host UUID from %s" % UUID_FILE)
    return NodeID


def is_host_local(host_id):
    return host_id == get_node_uuid()


def funcode(f):
    fc = getattr(f, 'func_code', None)
    if not fc:
        # python 3
        fc = f.__code__
    return fc


def memoize(f):
    fc = funcode(f)
    fn = fc.co_name

    def ff(self, *a, **kw):
        rv = getattr(self, '_' + fn, None)
        if rv is None:
            rv = f(self, *a, **kw)
            setattr(self, '_' + fn, rv)
        return rv
    return ff


def umask():
    return os.umask(0)


def entry2pb(e):
    return e.rsplit('/', 1)


def gauxpfx():
    return _CL_AUX_GFID_PFX


def md5hex(s):
    return md5(s).hexdigest()


def selfkill(sig=SIGTERM):
    os.kill(os.getpid(), sig)


def errno_wrap(call, arg=[], errnos=[], retry_errnos=[]):
    """ wrapper around calls resilient to errnos.
    """
    nr_tries = 0
    while True:
        try:
            return call(*arg)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno in errnos:
                return ex.errno
            if not ex.errno in retry_errnos:
                raise
            nr_tries += 1
            if nr_tries == GF_OP_RETRIES:
                # probably a screwed state, cannot do much...
                logging.warn('reached maximum retries (%s)...%s' %
                             (repr(arg), ex))
                return ex.errno
            time.sleep(0.250)  # retry the call


def lstat(e):
    return errno_wrap(os.lstat, [e], [ENOENT], [ESTALE])


class NoStimeAvailable(Exception):
    pass


class PartialHistoryAvailable(Exception):
    pass


class ChangelogHistoryNotAvailable(Exception):
    pass


class ChangelogException(OSError):
    pass


def gf_event(event_type, **kwargs):
    if EVENTS_ENABLED:
        from events.gf_event import gf_event as gfevent
        gfevent(event_type, **kwargs)


class GlusterLogLevel(object):
        NONE = 0
        EMERG = 1
        ALERT = 2
        CRITICAL = 3
        ERROR = 4
        WARNING = 5
        NOTICE = 6
        INFO = 7
        DEBUG = 8
        TRACE = 9


def get_changelog_log_level(lvl):
    return getattr(GlusterLogLevel, lvl, GlusterLogLevel.INFO)


def get_master_and_slave_data_from_args(args):
    master_name = None
    slave_data = None
    for arg in args:
        if arg.startswith(":"):
            master_name = arg.replace(":", "")
        if "::" in arg:
            slave_data = arg.replace("ssh://", "")

    return (master_name, slave_data)
