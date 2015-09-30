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

# auxillary gfid based access prefix
_CL_AUX_GFID_PFX = ".gfid/"
GF_OP_RETRIES = 20

CHANGELOG_AGENT_SERVER_VERSION = 1.0
CHANGELOG_AGENT_CLIENT_VERSION = 1.0


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
        shutil.rmtree(gconf.ssh_ctl_dir)
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
                    logging.warn("!!!!!!!!!!!!!")
                    logging.warn('!!! getting "No such file or directory" '
                                 "errors is most likely due to "
                                 "MISCONFIGURATION"
                                 ", please consult https://access.redhat.com"
                                 "/site/documentation/en-US/Red_Hat_Storage"
                                 "/2.1/html/Administration_Guide"
                                 "/chap-User_Guide-Geo_Rep-Preparation-"
                                 "Settingup_Environment.html")
                    logging.warn("!!!!!!!!!!!!!")
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


def is_host_local(host):
    locaddr = False
    for ai in socket.getaddrinfo(host, None):
        # cf. http://github.com/gluster/glusterfs/blob/ce111f47/xlators
        # /mgmt/glusterd/src/glusterd-utils.c#L125
        if ai[0] == socket.AF_INET:
            if ai[-1][0].split(".")[0] == "127":
                locaddr = True
                break
        elif ai[0] == socket.AF_INET6:
            if ai[-1][0] == "::1":
                locaddr = True
                break
        else:
            continue
        try:
            # use ICMP socket to avoid net.ipv4.ip_nonlocal_bind issue,
            # cf. https://bugzilla.redhat.com/show_bug.cgi?id=890587
            s = socket.socket(ai[0], socket.SOCK_RAW, socket.IPPROTO_ICMP)
        except socket.error:
            ex = sys.exc_info()[1]
            if ex.errno != EPERM:
                raise
            f = None
            try:
                f = open("/proc/sys/net/ipv4/ip_nonlocal_bind")
                if int(f.read()) != 0:
                    raise GsyncdError(
                        "non-local bind is set and not allowed to create "
                        "raw sockets, cannot determine if %s is local" % host)
                s = socket.socket(ai[0], socket.SOCK_DGRAM)
            finally:
                if f:
                    f.close()
        try:
            s.bind(ai[-1])
            locaddr = True
            break
        except:
            pass
        s.close()
    return locaddr


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


def errno_wrap(call, arg=[], errnos=[], retry_errnos=[ESTALE]):
    """ wrapper around calls resilient to errnos.
    retry in case of ESTALE by default.
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
                logging.warn('reached maximum retries (%s)...' % repr(arg))
                return
            time.sleep(0.250)  # retry the call


def lstat(e):
    try:
        return os.lstat(e)
    except (IOError, OSError):
        ex = sys.exc_info()[1]
        if ex.errno == ENOENT:
            return ex.errno
        else:
            raise


class NoPurgeTimeAvailable(Exception):
    pass


class PartialHistoryAvailable(Exception):
    pass


class ChangelogException(OSError):
    pass
