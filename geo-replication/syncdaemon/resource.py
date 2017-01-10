#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import re
import os
import sys
import stat
import time
import signal
import fcntl
import errno
import types
import struct
import socket
import logging
import tempfile
import threading
import subprocess
from errno import EEXIST, ENOENT, ENODATA, ENOTDIR, ELOOP
from errno import EISDIR, ENOTEMPTY, ESTALE, EINVAL
from select import error as SelectError
import shutil

from gconf import gconf
import repce
from repce import RepceServer, RepceClient
from master import gmaster_builder
import syncdutils
from syncdutils import GsyncdError, select, privileged, boolify, funcode
from syncdutils import umask, entry2pb, gauxpfx, errno_wrap, lstat
from syncdutils import NoStimeAvailable, PartialHistoryAvailable
from syncdutils import ChangelogException, ChangelogHistoryNotAvailable
from syncdutils import get_changelog_log_level
from syncdutils import CHANGELOG_AGENT_CLIENT_VERSION
from gsyncdstatus import GeorepStatus
from syncdutils import get_master_and_slave_data_from_args

UrlRX = re.compile('\A(\w+)://([^ *?[]*)\Z')
HostRX = re.compile('[a-zA-Z\d](?:[a-zA-Z\d.-]*[a-zA-Z\d])?', re.I)
UserRX = re.compile("[\w!\#$%&'*+-\/=?^_`{|}~]+")


def sup(x, *a, **kw):
    """a rubyesque "super" for python ;)

    invoke caller method in parent class with given args.
    """
    return getattr(super(type(x), x),
                   sys._getframe(1).f_code.co_name)(*a, **kw)


def desugar(ustr):
    """transform sugared url strings to standard <scheme>://<urlbody> form

    parsing logic enforces the constraint that sugared forms should contatin
    a ':' or a '/', which ensures that sugared urls do not conflict with
    gluster volume names.
    """
    m = re.match('([^:]*):(.*)', ustr)
    if m:
        if not m.groups()[0]:
            return "gluster://localhost" + ustr
        elif '@' in m.groups()[0] or re.search('[:/]', m.groups()[1]):
            return "ssh://" + ustr
        else:
            return "gluster://" + ustr
    else:
        if ustr[0] != '/':
            raise GsyncdError("cannot resolve sugared url '%s'" % ustr)
        ap = os.path.normpath(ustr)
        if ap.startswith('//'):
            ap = ap[1:]
        return "file://" + ap


def gethostbyname(hnam):
    """gethostbyname wrapper"""
    try:
        return socket.gethostbyname(hnam)
    except socket.gaierror:
        ex = sys.exc_info()[1]
        raise GsyncdError("failed to resolve %s: %s" %
                          (hnam, ex.strerror))


def parse_url(ustr):
    """instantiate an url object by scheme-to-class dispatch

    The url classes taken into consideration are the ones in
    this module whose names are full-caps.
    """
    m = UrlRX.match(ustr)
    if not m:
        ustr = desugar(ustr)
    m = UrlRX.match(ustr)
    if not m:
        raise GsyncdError("malformed url")
    sch, path = m.groups()
    this = sys.modules[__name__]
    if not hasattr(this, sch.upper()):
        raise GsyncdError("unknown url scheme " + sch)
    return getattr(this, sch.upper())(path)


class _MetaXattr(object):

    """singleton class, a lazy wrapper around the
    libcxattr module

    libcxattr (a heavy import due to ctypes) is
    loaded only when when the single
    instance is tried to be used.

    This reduces runtime for those invocations
    which do not need filesystem manipulation
    (eg. for config, url parsing)
    """

    def __getattr__(self, meth):
        from libcxattr import Xattr as LXattr
        xmeth = [m for m in dir(LXattr) if m[0] != '_']
        if not meth in xmeth:
            return
        for m in xmeth:
            setattr(self, m, getattr(LXattr, m))
        return getattr(self, meth)


Xattr = _MetaXattr()


class Popen(subprocess.Popen):

    """customized subclass of subprocess.Popen with a ring
    buffer for children error output"""

    @classmethod
    def init_errhandler(cls):
        """start the thread which handles children's error output"""
        cls.errstore = {}

        def tailer():
            while True:
                errstore = cls.errstore.copy()
                try:
                    poe, _, _ = select(
                        [po.stderr for po in errstore], [], [], 1)
                except (ValueError, SelectError):
                    # stderr is already closed wait for some time before
                    # checking next error
                    time.sleep(0.5)
                    continue
                for po in errstore:
                    if po.stderr not in poe:
                        continue
                    po.lock.acquire()
                    try:
                        if po.on_death_row:
                            continue
                        la = errstore[po]
                        try:
                            fd = po.stderr.fileno()
                        except ValueError:  # file is already closed
                            time.sleep(0.5)
                            continue

                        try:
                            l = os.read(fd, 1024)
                        except OSError:
                            time.sleep(0.5)
                            continue

                        if not l:
                            continue
                        tots = len(l)
                        for lx in la:
                            tots += len(lx)
                        while tots > 1 << 20 and la:
                            tots -= len(la.pop(0))
                        la.append(l)
                    finally:
                        po.lock.release()
        t = syncdutils.Thread(target=tailer)
        t.start()
        cls.errhandler = t

    @classmethod
    def fork(cls):
        """fork wrapper that restarts errhandler thread in child"""
        pid = os.fork()
        if not pid:
            cls.init_errhandler()
        return pid

    def __init__(self, args, *a, **kw):
        """customizations for subprocess.Popen instantiation

        - 'close_fds' is taken to be the default
        - if child's stderr is chosen to be managed,
          register it with the error handler thread
        """
        self.args = args
        if 'close_fds' not in kw:
            kw['close_fds'] = True
        self.lock = threading.Lock()
        self.on_death_row = False
        self.elines = []
        try:
            sup(self, args, *a, **kw)
        except:
            ex = sys.exc_info()[1]
            if not isinstance(ex, OSError):
                raise
            raise GsyncdError("""execution of "%s" failed with %s (%s)""" %
                              (args[0], errno.errorcode[ex.errno],
                               os.strerror(ex.errno)))
        if kw.get('stderr') == subprocess.PIPE:
            assert(getattr(self, 'errhandler', None))
            self.errstore[self] = []

    def errlog(self):
        """make a log about child's failure event"""
        filling = ""
        if self.elines:
            filling = ", saying:"
        logging.error("""command "%s" returned with %s%s""" %
                      (" ".join(self.args), repr(self.returncode), filling))
        lp = ''

        def logerr(l):
            logging.error(self.args[0] + "> " + l)
        for l in self.elines:
            ls = l.split('\n')
            ls[0] = lp + ls[0]
            lp = ls.pop()
            for ll in ls:
                logerr(ll)
        if lp:
            logerr(lp)

    def errfail(self):
        """fail nicely if child did not terminate with success"""
        self.errlog()
        syncdutils.finalize(exval=1)

    def terminate_geterr(self, fail_on_err=True):
        """kill child, finalize stderr harvesting (unregister
        from errhandler, set up .elines), fail on error if
        asked for
        """
        self.lock.acquire()
        try:
            self.on_death_row = True
        finally:
            self.lock.release()
        elines = self.errstore.pop(self)
        if self.poll() is None:
            self.terminate()
            if self.poll() is None:
                time.sleep(0.1)
                self.kill()
                self.wait()
        while True:
            if not select([self.stderr], [], [], 0.1)[0]:
                break
            b = os.read(self.stderr.fileno(), 1024)
            if b:
                elines.append(b)
            else:
                break
        self.stderr.close()
        self.elines = elines
        if fail_on_err and self.returncode != 0:
            self.errfail()


class Server(object):

    """singleton implemening those filesystem access primitives
       which are needed for geo-replication functionality

    (Singleton in the sense it's a class which has only static
    and classmethods and is used directly, without instantiation.)
    """

    GX_NSPACE_PFX = (privileged() and "trusted" or "system")
    GX_NSPACE = GX_NSPACE_PFX + ".glusterfs"
    NTV_FMTSTR = "!" + "B" * 19 + "II"
    FRGN_XTRA_FMT = "I"
    FRGN_FMTSTR = NTV_FMTSTR + FRGN_XTRA_FMT
    GX_GFID_CANONICAL_LEN = 37  # canonical gfid len + '\0'

    # for backend gfid fetch, do not use GX_NSPACE_PFX
    GFID_XATTR = 'trusted.gfid'
    GFID_FMTSTR = "!" + "B" * 16

    local_path = ''

    @classmethod
    def _fmt_mknod(cls, l):
        return "!II%dsI%dsIII" % (cls.GX_GFID_CANONICAL_LEN, l + 1)

    @classmethod
    def _fmt_mkdir(cls, l):
        return "!II%dsI%dsII" % (cls.GX_GFID_CANONICAL_LEN, l + 1)

    @classmethod
    def _fmt_symlink(cls, l1, l2):
        return "!II%dsI%ds%ds" % (cls.GX_GFID_CANONICAL_LEN, l1 + 1, l2 + 1)

    def _pathguard(f):
        """decorator method that checks
        the path argument of the decorated
        functions to make sure it does not
        point out of the managed tree
        """

        fc = funcode(f)
        pi = list(fc.co_varnames).index('path')

        def ff(*a):
            path = a[pi]
            ps = path.split('/')
            if path[0] == '/' or '..' in ps:
                raise ValueError('unsafe path')
            a = list(a)
            a[pi] = os.path.join(a[0].local_path, path)
            return f(*a)
        return ff

    @classmethod
    @_pathguard
    def entries(cls, path):
        """directory entries in an array"""
        # prevent symlinks being followed
        if not stat.S_ISDIR(os.lstat(path).st_mode):
            raise OSError(ENOTDIR, os.strerror(ENOTDIR))
        return os.listdir(path)

    @classmethod
    @_pathguard
    def lstat(cls, path):
        try:
            return os.lstat(path)
        except (IOError, OSError):
            ex = sys.exc_info()[1]
            if ex.errno == ENOENT:
                return ex.errno
            else:
                raise

    @classmethod
    @_pathguard
    def linkto_check(cls, path):
        try:
            return not (
                Xattr.lgetxattr_buf(path,
                                    'trusted.glusterfs.dht.linkto') == '')
        except (IOError, OSError):
            ex = sys.exc_info()[1]
            if ex.errno in (ENOENT, ENODATA):
                return False
            else:
                raise

    @classmethod
    @_pathguard
    def gfid(cls, path):
        try:
            buf = Xattr.lgetxattr(path, cls.GFID_XATTR, 16)
            m = re.match('(.{8})(.{4})(.{4})(.{4})(.{12})', "".join(
                ['%02x' % x for x in struct.unpack(cls.GFID_FMTSTR, buf)]))
            return '-'.join(m.groups())
        except (IOError, OSError):
            ex = sys.exc_info()[1]
            if ex.errno == ENOENT:
                return ex.errno
            else:
                raise

    @classmethod
    def gfid_mnt(cls, gfidpath):
        return errno_wrap(Xattr.lgetxattr,
                          [gfidpath, 'glusterfs.gfid.string',
                           cls.GX_GFID_CANONICAL_LEN], [ENOENT], [ESTALE])

    @classmethod
    @_pathguard
    def purge(cls, path, entries=None):
        """force-delete subtrees

        If @entries is not specified, delete
        the whole subtree under @path (including
        @path).

        Otherwise, @entries should be a
        a sequence of children of @path, and
        the effect is identical with a joint
        @entries-less purge on them, ie.

        for e in entries:
            cls.purge(os.path.join(path, e))
        """
        me_also = entries is None
        if not entries:
            try:
                # if it's a symlink, prevent
                # following it
                try:
                    os.unlink(path)
                    return
                except OSError:
                    ex = sys.exc_info()[1]
                    if ex.errno == EISDIR:
                        entries = os.listdir(path)
                    else:
                        raise
            except OSError:
                ex = sys.exc_info()[1]
                if ex.errno in (ENOTDIR, ENOENT, ELOOP):
                    try:
                        os.unlink(path)
                        return
                    except OSError:
                        ex = sys.exc_info()[1]
                        if ex.errno == ENOENT:
                            return
                        raise
                else:
                    raise
        for e in entries:
            cls.purge(os.path.join(path, e))
        if me_also:
            os.rmdir(path)

    @classmethod
    @_pathguard
    def _create(cls, path, ctor):
        """path creation backend routine"""
        try:
            ctor(path)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno == EEXIST:
                cls.purge(path)
                return ctor(path)
            raise

    @classmethod
    @_pathguard
    def mkdir(cls, path):
        cls._create(path, os.mkdir)

    @classmethod
    @_pathguard
    def symlink(cls, lnk, path):
        cls._create(path, lambda p: os.symlink(lnk, p))

    @classmethod
    @_pathguard
    def xtime(cls, path, uuid):
        """query xtime extended attribute

        Return xtime of @path for @uuid as a pair of integers.
        "Normal" errors due to non-existent @path or extended attribute
        are tolerated and errno is returned in such a case.
        """

        try:
            val = Xattr.lgetxattr(path,
                                  '.'.join([cls.GX_NSPACE, uuid, 'xtime']),
                                  8)
            return struct.unpack('!II', val)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno in (ENOENT, ENODATA, ENOTDIR):
                return ex.errno
            else:
                raise

    @classmethod
    @_pathguard
    def stime_mnt(cls, path, uuid):
        """query xtime extended attribute

        Return xtime of @path for @uuid as a pair of integers.
        "Normal" errors due to non-existent @path or extended attribute
        are tolerated and errno is returned in such a case.
        """

        try:
            val = Xattr.lgetxattr(path,
                                  '.'.join([cls.GX_NSPACE, uuid, 'stime']),
                                  8)
            return struct.unpack('!II', val)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno in (ENOENT, ENODATA, ENOTDIR):
                return ex.errno
            else:
                raise

    @classmethod
    @_pathguard
    def stime(cls, path, uuid):
        """query xtime extended attribute

        Return xtime of @path for @uuid as a pair of integers.
        "Normal" errors due to non-existent @path or extended attribute
        are tolerated and errno is returned in such a case.
        """

        try:
            val = Xattr.lgetxattr(path,
                                  '.'.join([cls.GX_NSPACE, uuid, 'stime']),
                                  8)
            return struct.unpack('!II', val)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno in (ENOENT, ENODATA, ENOTDIR):
                return ex.errno
            else:
                raise

    @classmethod
    @_pathguard
    def entry_stime(cls, path, uuid):
        """
        entry_stime xattr to reduce the number of retry of Entry changes when
        Geo-rep worker crashes and restarts. entry_stime is updated after
        processing every changelog file. On failure and restart, worker only
        have to reprocess the last changelog for Entry ops.
        Xattr Key: <PFX>.<MASTERVOL_UUID>.<SLAVEVOL_UUID>.entry_stime
        """
        try:
            val = Xattr.lgetxattr(path,
                                  '.'.join([cls.GX_NSPACE, uuid,
                                            'entry_stime']),
                                  8)
            return struct.unpack('!II', val)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno in (ENOENT, ENODATA, ENOTDIR):
                return ex.errno
            else:
                raise

    @classmethod
    def node_uuid(cls, path='.'):
        try:
            uuid_l = Xattr.lgetxattr_buf(
                path, '.'.join([cls.GX_NSPACE, 'node-uuid']))
            return uuid_l[:-1].split(' ')
        except OSError:
            raise

    @classmethod
    @_pathguard
    def set_stime(cls, path, uuid, mark):
        """set @mark as stime for @uuid on @path"""
        errno_wrap(Xattr.lsetxattr,
                   [path,
                    '.'.join([cls.GX_NSPACE, uuid, 'stime']),
                    struct.pack('!II', *mark)],
                   [ENOENT],
                   [ESTALE, EINVAL])

    @classmethod
    @_pathguard
    def set_entry_stime(cls, path, uuid, mark):
        """set @mark as stime for @uuid on @path"""
        errno_wrap(Xattr.lsetxattr,
                   [path,
                    '.'.join([cls.GX_NSPACE, uuid, 'entry_stime']),
                    struct.pack('!II', *mark)],
                   [ENOENT],
                   [ESTALE, EINVAL])

    @classmethod
    @_pathguard
    def set_xtime(cls, path, uuid, mark):
        """set @mark as xtime for @uuid on @path"""
        errno_wrap(Xattr.lsetxattr,
                   [path,
                    '.'.join([cls.GX_NSPACE, uuid, 'xtime']),
                    struct.pack('!II', *mark)],
                   [ENOENT],
                   [ESTALE, EINVAL])

    @classmethod
    @_pathguard
    def set_xtime_remote(cls, path, uuid, mark):
        """
        set @mark as xtime for @uuid on @path
        the difference b/w this and set_xtime() being
        set_xtime() being overloaded to set the xtime
        on the brick (this method sets xtime on the
        remote slave)
        """
        Xattr.lsetxattr(
            path, '.'.join([cls.GX_NSPACE, uuid, 'xtime']),
            struct.pack('!II', *mark))

    @classmethod
    def entry_ops(cls, entries):
        pfx = gauxpfx()
        logging.debug('entries: %s' % repr(entries))
        # regular file

        def entry_pack_reg(gf, bn, mo, uid, gid):
            blen = len(bn)
            return struct.pack(cls._fmt_mknod(blen),
                               uid, gid, gf, mo, bn,
                               stat.S_IMODE(mo), 0, umask())

        def entry_pack_reg_stat(gf, bn, st):
            blen = len(bn)
            mo = st['mode']
            return struct.pack(cls._fmt_mknod(blen),
                               st['uid'], st['gid'],
                               gf, mo, bn,
                               stat.S_IMODE(mo), 0, umask())
        # mkdir

        def entry_pack_mkdir(gf, bn, mo, uid, gid):
            blen = len(bn)
            return struct.pack(cls._fmt_mkdir(blen),
                               uid, gid, gf, mo, bn,
                               stat.S_IMODE(mo), umask())
        # symlink

        def entry_pack_symlink(gf, bn, lnk, st):
            blen = len(bn)
            llen = len(lnk)
            return struct.pack(cls._fmt_symlink(blen, llen),
                               st['uid'], st['gid'],
                               gf, st['mode'], bn, lnk)

        def entry_purge(entry, gfid):
            # This is an extremely racy code and needs to be fixed ASAP.
            # The GFID check here is to be sure that the pargfid/bname
            # to be purged is the GFID gotten from the changelog.
            # (a stat(changelog_gfid) would also be valid here)
            # The race here is between the GFID check and the purge.
            if not matching_disk_gfid(gfid, entry):
                return

            er = errno_wrap(os.unlink, [entry], [ENOENT, ESTALE, EISDIR])
            if isinstance(er, int):
                if er == EISDIR:
                    er = errno_wrap(os.rmdir, [entry], [ENOENT, ESTALE,
                                                        ENOTEMPTY])
                    if er == ENOTEMPTY:
                        return er

        def collect_failure(e, cmd_ret):
            # We do this for failing fops on Slave
            # Master should be logging this
            if cmd_ret is None:
                return False

            if cmd_ret == EEXIST:
                disk_gfid = cls.gfid_mnt(e['entry'])
                if isinstance(disk_gfid, basestring) and e['gfid'] != disk_gfid:
                    failures.append((e, cmd_ret, disk_gfid))
                else:
                    return False
            else:
                failures.append((e, cmd_ret))

            return True

        failures = []

        def matching_disk_gfid(gfid, entry):
            disk_gfid = cls.gfid_mnt(entry)
            if isinstance(disk_gfid, int):
                return False

            if not gfid == disk_gfid:
                return False

            return True

        def recursive_rmdir(gfid, entry, path):
            """disk_gfid check added for original path for which
            recursive_delete is called. This disk gfid check executed
            before every Unlink/Rmdir. If disk gfid is not matching
            with GFID from Changelog, that means other worker
            deleted the directory. Even if the subdir/file present,
            it belongs to different parent. Exit without performing
            further deletes.
            """
            if not matching_disk_gfid(gfid, entry):
                return

            names = []
            names = errno_wrap(os.listdir, [path], [ENOENT], [ESTALE])
            if isinstance(names, int):
                return

            for name in names:
                fullname = os.path.join(path, name)
                if not matching_disk_gfid(gfid, entry):
                    return
                er = errno_wrap(os.remove, [fullname], [ENOENT, ESTALE,
                                                        EISDIR])

                if er == EISDIR:
                    recursive_rmdir(gfid, entry, fullname)

            if not matching_disk_gfid(gfid, entry):
                return

            errno_wrap(os.rmdir, [path], [ENOENT, ESTALE])

        def rename_with_disk_gfid_confirmation(gfid, entry, en):
            if not matching_disk_gfid(gfid, entry):
                logging.error("RENAME ignored: "
                              "source entry:%s(gfid:%s) does not match with "
                              "on-disk gfid(%s), when attempting to rename "
                              "to %s" %
                              (entry, gfid, cls.gfid_mnt(entry), en))
                return

            cmd_ret = errno_wrap(os.rename,
                                 [entry, en],
                                 [ENOENT, EEXIST], [ESTALE])
            collect_failure(e, cmd_ret)


        for e in entries:
            blob = None
            op = e['op']
            gfid = e['gfid']
            entry = e['entry']
            uid = 0
            gid = 0
            if e.get("stat", {}):
                # Copy UID/GID value and then reset to zero. Copied UID/GID
                # will be used to run chown once entry is created.
                uid = e['stat']['uid']
                gid = e['stat']['gid']
                e['stat']['uid'] = 0
                e['stat']['gid'] = 0

            (pg, bname) = entry2pb(entry)
            if op in ['RMDIR', 'UNLINK']:
                # Try once, if rmdir failed with ENOTEMPTY
                # then delete recursively.
                er = entry_purge(entry, gfid)
                if isinstance(er, int):
                    if er == ENOTEMPTY and op == 'RMDIR':
                        # Retry if ENOTEMPTY, ESTALE
                        er1 = errno_wrap(recursive_rmdir,
                                         [gfid, entry,
                                          os.path.join(pg, bname)],
                                         [], [ENOTEMPTY, ESTALE, ENODATA])
                        if not isinstance(er1, int):
                            logging.debug("Removed %s => %s/%s recursively" %
                                          (gfid, pg, bname))
                        else:
                            logging.warn("Recursive remove %s => %s/%s"
                                         "failed: %s" % (gfid, pg, bname,
                                                         os.strerror(er1)))
                    else:
                        logging.warn("Failed to remove %s => %s/%s. %s" %
                                     (gfid, pg, bname, os.strerror(er)))
            elif op in ['CREATE', 'MKNOD']:
                slink = os.path.join(pfx, gfid)
                st = lstat(slink)
                # don't create multiple entries with same gfid
                if isinstance(st, int):
                    blob = entry_pack_reg(
                        gfid, bname, e['mode'], e['uid'], e['gid'])
            elif op == 'MKDIR':
                slink = os.path.join(pfx, gfid)
                st = lstat(slink)
                # don't create multiple entries with same gfid
                if isinstance(st, int):
                    blob = entry_pack_mkdir(
                        gfid, bname, e['mode'], e['uid'], e['gid'])
            elif op == 'LINK':
                slink = os.path.join(pfx, gfid)
                st = lstat(slink)
                if isinstance(st, int):
                    (pg, bname) = entry2pb(entry)
                    blob = entry_pack_reg_stat(gfid, bname, e['stat'])
                else:
                    cmd_ret = errno_wrap(os.link,
                                         [slink, entry],
                                         [ENOENT, EEXIST], [ESTALE])
                    collect_failure(e, cmd_ret)
            elif op == 'SYMLINK':
                blob = entry_pack_symlink(gfid, bname, e['link'], e['stat'])
            elif op == 'RENAME':
                en = e['entry1']
                st = lstat(entry)
                if isinstance(st, int):
                    if e['stat'] and not stat.S_ISDIR(e['stat']['mode']):
                        if stat.S_ISLNK(e['stat']['mode']) and \
                           e['link'] is not None:
                            (pg, bname) = entry2pb(en)
                            blob = entry_pack_symlink(gfid, bname,
                                                      e['link'], e['stat'])
                        else:
                            (pg, bname) = entry2pb(en)
                            blob = entry_pack_reg_stat(gfid, bname, e['stat'])
                else:
                    st1 = lstat(en)
                    if isinstance(st1, int):
                        rename_with_disk_gfid_confirmation(gfid, entry, en)
                    else:
                        if st.st_ino == st1.st_ino:
                            # we have a hard link, we can now unlink source
                            try:
                                errno_wrap(os.unlink, [entry],
                                           [ENOENT, ESTALE])
                            except OSError as e:
                                if e.errno == EISDIR:
                                    try:
                                        errno_wrap(os.rmdir, [entry],
                                                   [ENOENT, ESTALE])
                                    except OSError as e:
                                        if e.errno == ENOTEMPTY:
                                            logging.error(
                                                "Unable to delete directory "
                                                "{0}, Both Old({1}) and New{2}"
                                                " directories exists".format(
                                                    entry, entry, en))
                                        else:
                                            raise
                                else:
                                    raise
                        else:
                            rename_with_disk_gfid_confirmation(gfid, entry, en)
            if blob:
                cmd_ret = errno_wrap(Xattr.lsetxattr,
                                     [pg, 'glusterfs.gfid.newfile', blob],
                                     [EEXIST, ENOENT],
                                     [ESTALE, EINVAL])
                failed = collect_failure(e, cmd_ret)

                # If directory creation is failed, return immediately before
                # further processing. Allowing it to further process will
                # cause the entire directory tree to fail syncing to slave.
                # Hence master will log and raise exception if it's
                # directory failure.
                if failed and op == 'MKDIR':
                    return failures

                # If UID/GID is different than zero that means we are trying
                # create Entry with different UID/GID. Create Entry with
                # UID:0 and GID:0, and then call chown to set UID/GID
                if uid != 0 or gid != 0:
                    path = os.path.join(pfx, gfid)
                    cmd_ret = errno_wrap(os.chown, [path, uid, gid], [ENOENT],
                                         [ESTALE, EINVAL])
                    collect_failure(e, cmd_ret)

        return failures

    @classmethod
    def meta_ops(cls, meta_entries):
        logging.debug('Meta-entries: %s' % repr(meta_entries))
        failures = []
        for e in meta_entries:
            mode = e['stat']['mode']
            uid = e['stat']['uid']
            gid = e['stat']['gid']
            atime = e['stat']['atime']
            mtime = e['stat']['mtime']
            go = e['go']
            cmd_ret = errno_wrap(os.chmod, [go, mode],
                                 [ENOENT], [ESTALE, EINVAL])
            # This is a fail fast mechanism
            # We do this for failing fops on Slave
            # Master should be logging this
            if isinstance(cmd_ret, int):
                failures.append((e, cmd_ret))
                continue
            errno_wrap(os.chown, [go, uid, gid], [ENOENT], [ESTALE, EINVAL])
            errno_wrap(os.utime, [go, (atime, mtime)],
                       [ENOENT], [ESTALE, EINVAL])
        return failures

    @classmethod
    @_pathguard
    def setattr(cls, path, adct):
        """set file attributes

        @adct is a dict, where 'own', 'mode' and 'times'
        keys are looked for and values used to perform
        chown, chmod or utimes on @path.
        """
        own = adct.get('own')
        if own:
            os.lchown(path, *own)
        mode = adct.get('mode')
        if mode:
            os.chmod(path, stat.S_IMODE(mode))
        times = adct.get('times')
        if times:
            os.utime(path, times)

    @staticmethod
    def pid():
        return os.getpid()

    last_keep_alive = 0

    @classmethod
    def keep_alive(cls, dct):
        """process keepalive messages.

        Return keep-alive counter (number of received keep-alive
        messages).

        Now the "keep-alive" message can also have a payload which is
        used to set a foreign volume-mark on the underlying file system.
        """
        if dct:
            key = '.'.join([cls.GX_NSPACE, 'volume-mark', dct['uuid']])
            val = struct.pack(cls.FRGN_FMTSTR,
                              *(dct['version'] +
                                tuple(int(x, 16)
                                      for x in re.findall('(?:[\da-f]){2}',
                                                          dct['uuid'])) +
                                (dct['retval'],) + dct['volume_mark'][0:2] + (
                                    dct['timeout'],)))
            Xattr.lsetxattr('.', key, val)
        cls.last_keep_alive += 1
        return cls.last_keep_alive

    @staticmethod
    def version():
        """version used in handshake"""
        return 1.0


class SlaveLocal(object):

    """mix-in class to implement some factes of a slave server

    ("mix-in" is sort of like "abstract class", ie. it's not
    instantiated just included in the ancesty DAG. I use "mix-in"
    to indicate that it's not used as an abstract base class,
    rather just taken in to implement additional functionality
    on the basis of the assumed availability of certain interfaces.)
    """

    def can_connect_to(self, remote):
        """determine our position in the connectibility matrix"""
        return not remote

    def service_loop(self):
        """start a RePCe server serving self's server

        stop servicing if a timeout is configured and got no
        keep-alime in that inteval
        """

        if boolify(gconf.use_rsync_xattrs) and not privileged():
            raise GsyncdError(
                "using rsync for extended attributes is not supported")

        repce = RepceServer(
            self.server, sys.stdin, sys.stdout, int(gconf.sync_jobs))
        t = syncdutils.Thread(target=lambda: (repce.service_loop(),
                                              syncdutils.finalize()))
        t.start()
        logging.info("slave listening")
        if gconf.timeout and int(gconf.timeout) > 0:
            while True:
                lp = self.server.last_keep_alive
                time.sleep(int(gconf.timeout))
                if lp == self.server.last_keep_alive:
                    logging.info(
                        "connection inactive for %d seconds, stopping" %
                        int(gconf.timeout))
                    break
        else:
            select((), (), ())


class SlaveRemote(object):

    """mix-in class to implement an interface to a remote slave"""

    def connect_remote(self, rargs=[], **opts):
        """connects to a remote slave

        Invoke an auxiliary utility (slave gsyncd, possibly wrapped)
        which sets up the connection and set up a RePCe client to
        communicate throuh its stdio.
        """
        slave = opts.get('slave', self.url)
        extra_opts = []
        so = getattr(gconf, 'session_owner', None)
        if so:
            extra_opts += ['--session-owner', so]
        li = getattr(gconf, 'local_id', None)
        if li:
            extra_opts += ['--local-id', li]
        ln = getattr(gconf, 'local_node', None)
        if ln:
            extra_opts += ['--local-node', ln]
        if boolify(gconf.use_rsync_xattrs):
            extra_opts.append('--use-rsync-xattrs')
        po = Popen(rargs + gconf.remote_gsyncd.split() + extra_opts +
                   ['-N', '--listen', '--timeout', str(gconf.timeout),
                    slave],
                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE)
        gconf.transport = po
        return self.start_fd_client(po.stdout, po.stdin, **opts)

    def start_fd_client(self, i, o, **opts):
        """set up RePCe client, handshake with server

        It's cut out as a separate method to let
        subclasses hook into client startup
        """
        self.server = RepceClient(i, o)
        rv = self.server.__version__()
        exrv = {'proto': repce.repce_version, 'object': Server.version()}
        da0 = (rv, exrv)
        da1 = ({}, {})
        for i in range(2):
            for k, v in da0[i].iteritems():
                da1[i][k] = int(v)
        if da1[0] != da1[1]:
            raise GsyncdError(
                "RePCe major version mismatch: local %s, remote %s" %
                (exrv, rv))

    def rsync(self, files, *args, **kw):
        """invoke rsync"""
        if not files:
            raise GsyncdError("no files to sync")
        logging.debug("files: " + ", ".join(files))
        argv = gconf.rsync_command.split() + \
            ['-aR0', '--inplace', '--files-from=-', '--super',
             '--stats', '--numeric-ids', '--no-implied-dirs'] + \
            gconf.rsync_options.split() + \
            (boolify(gconf.sync_xattrs) and ['--xattrs'] or []) + \
            (boolify(gconf.sync_acls) and ['--acls'] or []) + \
            ['.'] + list(args)

        log_rsync_performance = boolify(gconf.configinterface.get_realtime(
            "log_rsync_performance", default_value=False))

        if log_rsync_performance:
            # use stdout=PIPE only when log_rsync_performance enabled
            # Else rsync will write to stdout and nobody is their
            # to consume. If PIPE is full rsync hangs.
            po = Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
        else:
            po = Popen(argv, stdin=subprocess.PIPE, stderr=subprocess.PIPE)

        for f in files:
            po.stdin.write(f)
            po.stdin.write('\0')

        stdout, stderr = po.communicate()

        if kw.get("log_err", False):
            for errline in stderr.strip().split("\n")[:-1]:
                logging.error("SYNC Error(Rsync): %s" % errline)

        if log_rsync_performance:
            rsync_msg = []
            for line in stdout.split("\n"):
                if line.startswith("Number of files:") or \
                   line.startswith("Number of regular files transferred:") or \
                   line.startswith("Total file size:") or \
                   line.startswith("Total transferred file size:") or \
                   line.startswith("Literal data:") or \
                   line.startswith("Matched data:") or \
                   line.startswith("Total bytes sent:") or \
                   line.startswith("Total bytes received:") or \
                   line.startswith("sent "):
                    rsync_msg.append(line)
            logging.info("rsync performance: %s" % ", ".join(rsync_msg))

        return po

    def tarssh(self, files, slaveurl, log_err=False):
        """invoke tar+ssh
        -z (compress) can be use if needed, but omitting it now
        as it results in weird error (tar+ssh errors out (errcode: 2)
        """
        if not files:
            raise GsyncdError("no files to sync")
        logging.debug("files: " + ", ".join(files))
        (host, rdir) = slaveurl.split(':')
        tar_cmd = ["tar"] + \
            ["--sparse", "-cf", "-", "--files-from", "-"]
        ssh_cmd = gconf.ssh_command_tar.split() + \
            ["-p", str(gconf.ssh_port)] + \
            [host, "tar"] + \
            ["--overwrite", "-xf", "-", "-C", rdir]
        p0 = Popen(tar_cmd, stdout=subprocess.PIPE,
                   stdin=subprocess.PIPE, stderr=subprocess.PIPE)
        p1 = Popen(ssh_cmd, stdin=p0.stdout, stderr=subprocess.PIPE)
        for f in files:
            p0.stdin.write(f)
            p0.stdin.write('\n')

        p0.stdin.close()
        p0.stdout.close()  # Allow p0 to receive a SIGPIPE if p1 exits.
        # wait for tar to terminate, collecting any errors, further
        # waiting for transfer to complete
        _, stderr1 = p1.communicate()

        # stdin and stdout of p0 is already closed, Reset to None and
        # wait for child process to complete
        p0.stdin = None
        p0.stdout = None
        p0.communicate()

        if log_err:
            for errline in stderr1.strip().split("\n")[:-1]:
                logging.error("SYNC Error(Untar): %s" % errline)

        return p1


class AbstractUrl(object):

    """abstract base class for url scheme classes"""

    def __init__(self, path, pattern):
        m = re.search(pattern, path)
        if not m:
            raise GsyncdError("malformed path")
        self.path = path
        return m.groups()

    @property
    def scheme(self):
        return type(self).__name__.lower()

    def canonical_path(self):
        return self.path

    def get_url(self, canonical=False, escaped=False):
        """format self's url in various styles"""
        if canonical:
            pa = self.canonical_path()
        else:
            pa = self.path
        u = "://".join((self.scheme, pa))
        if escaped:
            u = syncdutils.escape(u)
        return u

    @property
    def url(self):
        return self.get_url()


class FILE(AbstractUrl, SlaveLocal, SlaveRemote):

    """scheme class for file:// urls

    can be used to represent a file slave server
    on slave side, or interface to a remote file
    file server on master side
    """

    class FILEServer(Server):

        """included server flavor"""
        pass

    server = FILEServer

    def __init__(self, path):
        sup(self, path, '^/')

    def connect(self):
        """inhibit the resource beyond"""
        os.chdir(self.path)

    def rsync(self, files, log_err=False):
        return sup(self, files, self.path, log_err=log_err)


class GLUSTER(AbstractUrl, SlaveLocal, SlaveRemote):

    """scheme class for gluster:// urls

    can be used to represent a gluster slave server
    on slave side, or interface to a remote gluster
    slave on master side, or to represent master
    (slave-ish features come from the mixins, master
    functionality is outsourced to GMaster from master)
    """

    class GLUSTERServer(Server):

        "server enhancements for a glusterfs backend"""

        @classmethod
        def _attr_unpack_dict(cls, xattr, extra_fields=''):
            """generic volume mark fetching/parsing backed"""
            fmt_string = cls.NTV_FMTSTR + extra_fields
            buf = Xattr.lgetxattr('.', xattr, struct.calcsize(fmt_string))
            vm = struct.unpack(fmt_string, buf)
            m = re.match(
                '(.{8})(.{4})(.{4})(.{4})(.{12})',
                "".join(['%02x' % x for x in vm[2:18]]))
            uuid = '-'.join(m.groups())
            volinfo = {'version': vm[0:2],
                       'uuid': uuid,
                       'retval': vm[18],
                       'volume_mark': vm[19:21],
                       }
            if extra_fields:
                return volinfo, vm[-len(extra_fields):]
            else:
                return volinfo

        @classmethod
        def foreign_volume_infos(cls):
            """return list of valid (not expired) foreign volume marks"""
            dict_list = []
            xattr_list = Xattr.llistxattr_buf('.')
            for ele in xattr_list:
                if ele.find('.'.join([cls.GX_NSPACE, 'volume-mark', ''])) == 0:
                    d, x = cls._attr_unpack_dict(ele, cls.FRGN_XTRA_FMT)
                    now = int(time.time())
                    if x[0] > now:
                        logging.debug("volinfo[%s] expires: %d "
                                      "(%d sec later)" %
                                      (d['uuid'], x[0], x[0] - now))
                        d['timeout'] = x[0]
                        dict_list.append(d)
                    else:
                        try:
                            Xattr.lremovexattr('.', ele)
                        except OSError:
                            pass
            return dict_list

        @classmethod
        def native_volume_info(cls):
            """get the native volume mark of the underlying gluster volume"""
            try:
                return cls._attr_unpack_dict('.'.join([cls.GX_NSPACE,
                                                       'volume-mark']))
            except OSError:
                ex = sys.exc_info()[1]
                if ex.errno != ENODATA:
                    raise

    server = GLUSTERServer

    def __init__(self, path):
        self.host, self.volume = sup(self, path, '^(%s):(.+)' % HostRX.pattern)

    def canonical_path(self):
        return ':'.join([gethostbyname(self.host), self.volume])

    def can_connect_to(self, remote):
        """determine our position in the connectibility matrix"""
        return not remote or \
            (isinstance(remote, SSH) and isinstance(remote.inner_rsc, GLUSTER))

    class Mounter(object):

        """Abstract base class for mounter backends"""

        def __init__(self, params):
            self.params = params
            self.mntpt = None

        @classmethod
        def get_glusterprog(cls):
            return os.path.join(gconf.gluster_command_dir, cls.glusterprog)

        def umount_l(self, d):
            """perform lazy umount"""
            po = Popen(self.make_umount_argv(d), stderr=subprocess.PIPE)
            po.wait()
            return po

        @classmethod
        def make_umount_argv(cls, d):
            raise NotImplementedError

        def make_mount_argv(self, *a):
            raise NotImplementedError

        def cleanup_mntpt(self, *a):
            pass

        def handle_mounter(self, po):
            po.wait()

        def inhibit(self, *a):
            """inhibit a gluster filesystem

            Mount glusterfs over a temporary mountpoint,
            change into the mount, and lazy unmount the
            filesystem.
            """

            mpi, mpo = os.pipe()
            mh = Popen.fork()
            if mh:
                os.close(mpi)
                fcntl.fcntl(mpo, fcntl.F_SETFD, fcntl.FD_CLOEXEC)
                d = None
                margv = self.make_mount_argv(*a)
                if self.mntpt:
                    # mntpt is determined pre-mount
                    d = self.mntpt
                    os.write(mpo, d + '\0')
                po = Popen(margv, **self.mountkw)
                self.handle_mounter(po)
                po.terminate_geterr()
                logging.debug('auxiliary glusterfs mount in place')
                if not d:
                    # mntpt is determined during mount
                    d = self.mntpt
                    os.write(mpo, d + '\0')
                os.write(mpo, 'M')
                t = syncdutils.Thread(target=lambda: os.chdir(d))
                t.start()
                tlim = gconf.starttime + int(gconf.connection_timeout)
                while True:
                    if not t.isAlive():
                        break
                    if time.time() >= tlim:
                        syncdutils.finalize(exval=1)
                    time.sleep(1)
                os.close(mpo)
                _, rv = syncdutils.waitpid(mh, 0)
                if rv:
                    rv = (os.WIFEXITED(rv) and os.WEXITSTATUS(rv) or 0) - \
                         (os.WIFSIGNALED(rv) and os.WTERMSIG(rv) or 0)
                    logging.warn('stale mount possibly left behind on ' + d)
                    raise GsyncdError("cleaning up temp mountpoint %s "
                                      "failed with status %d" %
                                      (d, rv))
            else:
                rv = 0
                try:
                    os.setsid()
                    os.close(mpo)
                    mntdata = ''
                    while True:
                        c = os.read(mpi, 1)
                        if not c:
                            break
                        mntdata += c
                    if mntdata:
                        mounted = False
                        if mntdata[-1] == 'M':
                            mntdata = mntdata[:-1]
                            assert(mntdata)
                            mounted = True
                        assert(mntdata[-1] == '\0')
                        mntpt = mntdata[:-1]
                        assert(mntpt)
                        if mounted:
                            po = self.umount_l(mntpt)
                            po.terminate_geterr(fail_on_err=False)
                            if po.returncode != 0:
                                po.errlog()
                                rv = po.returncode
                        self.cleanup_mntpt(mntpt)
                except:
                    logging.exception('mount cleanup failure:')
                    rv = 200
                os._exit(rv)
            logging.debug('auxiliary glusterfs mount prepared')

    class DirectMounter(Mounter):

        """mounter backend which calls mount(8), umount(8) directly"""

        mountkw = {'stderr': subprocess.PIPE}
        glusterprog = 'glusterfs'

        @staticmethod
        def make_umount_argv(d):
            return ['umount', '-l', d]

        def make_mount_argv(self):
            self.mntpt = tempfile.mkdtemp(prefix='gsyncd-aux-mount-')
            return [self.get_glusterprog()] + \
                ['--' + p for p in self.params] + [self.mntpt]

        def cleanup_mntpt(self, mntpt=None):
            if not mntpt:
                mntpt = self.mntpt
            os.rmdir(mntpt)

    class MountbrokerMounter(Mounter):

        """mounter backend using the mountbroker gluster service"""

        mountkw = {'stderr': subprocess.PIPE, 'stdout': subprocess.PIPE}
        glusterprog = 'gluster'

        @classmethod
        def make_cli_argv(cls):
            return [cls.get_glusterprog()] + ['--remote-host=localhost'] + \
                gconf.gluster_cli_options.split() + ['system::']

        @classmethod
        def make_umount_argv(cls, d):
            return cls.make_cli_argv() + ['umount', d, 'lazy']

        def make_mount_argv(self, label):
            return self.make_cli_argv() + \
                ['mount', label, 'user-map-root=' +
                    syncdutils.getusername()] + self.params

        def handle_mounter(self, po):
            self.mntpt = po.stdout.readline()[:-1]
            po.stdout.close()
            sup(self, po)
            if po.returncode != 0:
                # if cli terminated with error due to being
                # refused by glusterd, what it put
                # out on stdout is a diagnostic message
                logging.error('glusterd answered: %s' % self.mntpt)

    def connect(self):
        """inhibit the resource beyond

        Choose mounting backend (direct or mountbroker),
        set up glusterfs parameters and perform the mount
        with given backend
        """

        label = getattr(gconf, 'mountbroker', None)
        if not label and not privileged():
            label = syncdutils.getusername()
        mounter = label and self.MountbrokerMounter or self.DirectMounter
        params = gconf.gluster_params.split() + \
            (gconf.gluster_log_level and ['log-level=' +
                                          gconf.gluster_log_level] or []) + \
            ['log-file=' + gconf.gluster_log_file, 'volfile-server=' +
             self.host, 'volfile-id=' + self.volume, 'client-pid=-1']
        mounter(params).inhibit(*[l for l in [label] if l])

    def connect_remote(self, *a, **kw):
        sup(self, *a, **kw)
        self.slavedir = "/proc/%d/cwd" % self.server.pid()

    def gmaster_instantiate_tuple(self, slave):
        """return a tuple of the 'one shot' and the 'main crawl'
        class instance"""
        return (gmaster_builder('xsync')(self, slave),
                gmaster_builder()(self, slave),
                gmaster_builder('changeloghistory')(self, slave))

    def service_loop(self, *args):
        """enter service loop

        - if slave given, instantiate GMaster and
          pass control to that instance, which implements
          master behavior
        - else do that's what's inherited
        """
        if args:
            slave = args[0]
            if gconf.local_path:
                class brickserver(FILE.FILEServer):
                    local_path = gconf.local_path
                    aggregated = self.server

                    @classmethod
                    def entries(cls, path):
                        e = super(brickserver, cls).entries(path)
                        # on the brick don't mess with /.glusterfs
                        if path == '.':
                            try:
                                e.remove('.glusterfs')
                                e.remove('.trashcan')
                            except ValueError:
                                pass
                        return e

                    @classmethod
                    def lstat(cls, e):
                        """ path based backend stat """
                        return super(brickserver, cls).lstat(e)

                    @classmethod
                    def gfid(cls, e):
                        """ path based backend gfid fetch """
                        return super(brickserver, cls).gfid(e)

                    @classmethod
                    def linkto_check(cls, e):
                        return super(brickserver, cls).linkto_check(e)
                if gconf.slave_id:
                    # define {,set_}xtime in slave, thus preempting
                    # the call to remote, so that it takes data from
                    # the local brick
                    slave.server.xtime = types.MethodType(
                        lambda _self, path, uuid: (
                            brickserver.xtime(path,
                                              uuid + '.' + gconf.slave_id)
                        ),
                        slave.server)
                    slave.server.stime = types.MethodType(
                        lambda _self, path, uuid: (
                            brickserver.stime(path,
                                              uuid + '.' + gconf.slave_id)
                        ),
                        slave.server)
                    slave.server.entry_stime = types.MethodType(
                        lambda _self, path, uuid: (
                            brickserver.entry_stime(
                                path,
                                uuid + '.' + gconf.slave_id)
                        ),
                        slave.server)
                    slave.server.set_stime = types.MethodType(
                        lambda _self, path, uuid, mark: (
                            brickserver.set_stime(path,
                                                  uuid + '.' + gconf.slave_id,
                                                  mark)
                        ),
                        slave.server)
                    slave.server.set_entry_stime = types.MethodType(
                        lambda _self, path, uuid, mark: (
                            brickserver.set_entry_stime(
                                path,
                                uuid + '.' + gconf.slave_id,
                                mark)
                        ),
                        slave.server)
                (g1, g2, g3) = self.gmaster_instantiate_tuple(slave)
                g1.master.server = brickserver
                g2.master.server = brickserver
                g3.master.server = brickserver
            else:
                (g1, g2, g3) = self.gmaster_instantiate_tuple(slave)
                g1.master.server.aggregated = gmaster.master.server
                g2.master.server.aggregated = gmaster.master.server
                g3.master.server.aggregated = gmaster.master.server
            # bad bad bad: bad way to do things like this
            # need to make this elegant
            # register the crawlers and start crawling
            # g1 ==> Xsync, g2 ==> config.change_detector(changelog by default)
            # g3 ==> changelog History
            changelog_register_failed = False
            (inf, ouf, ra, wa) = gconf.rpc_fd.split(',')
            changelog_agent = RepceClient(int(inf), int(ouf))
            master_name, slave_data = get_master_and_slave_data_from_args(
                sys.argv)
            status = GeorepStatus(gconf.state_file, gconf.local_node,
                                  gconf.local_path,
                                  gconf.local_node_id,
                                  master_name, slave_data)
            status.reset_on_worker_start()
            rv = changelog_agent.version()
            if int(rv) != CHANGELOG_AGENT_CLIENT_VERSION:
                raise GsyncdError(
                    "RePCe major version mismatch(changelog agent): "
                    "local %s, remote %s" %
                    (CHANGELOG_AGENT_CLIENT_VERSION, rv))

            try:
                workdir = g2.setup_working_dir()
                # Register only when change_detector is not set to
                # xsync, else agent will generate changelog files
                # in .processing directory of working dir
                if gconf.change_detector != 'xsync':
                    # register with the changelog library
                    # 9 == log level (DEBUG)
                    # 5 == connection retries
                    changelog_agent.init()
                    changelog_agent.register(gconf.local_path,
                                             workdir, gconf.changelog_log_file,
                                             get_changelog_log_level(
                                                 gconf.changelog_log_level),
                                             g2.CHANGELOG_CONN_RETRIES)

                register_time = int(time.time())
                g2.register(register_time, changelog_agent, status)
                g3.register(register_time, changelog_agent, status)
            except ChangelogException as e:
                logging.error("Changelog register failed, %s" % e)
                sys.exit(1)

            g1.register(status=status)
            logging.info("Register time: %s" % register_time)
            # oneshot: Try to use changelog history api, if not
            # available switch to FS crawl
            # Note: if config.change_detector is xsync then
            # it will not use changelog history api
            try:
                g3.crawlwrap(oneshot=True)
            except PartialHistoryAvailable as e:
                logging.info('Partial history available, using xsync crawl'
                             ' after consuming history till %s' % str(e))
                g1.crawlwrap(oneshot=True, register_time=register_time)
            except ChangelogHistoryNotAvailable:
                logging.info('Changelog history not available, using xsync')
                g1.crawlwrap(oneshot=True, register_time=register_time)
            except NoStimeAvailable:
                logging.info('No stime available, using xsync crawl')
                g1.crawlwrap(oneshot=True, register_time=register_time)
            except ChangelogException as e:
                logging.error("Changelog History Crawl failed, %s" % e)
                sys.exit(1)

            try:
                g2.crawlwrap()
            except ChangelogException as e:
                logging.error("Changelog crawl failed, %s" % e)
                sys.exit(1)
        else:
            sup(self, *args)

    def rsync(self, files, log_err=False):
        return sup(self, files, self.slavedir, log_err=log_err)

    def tarssh(self, files, log_err=False):
        return sup(self, files, self.slavedir, log_err=log_err)


class SSH(AbstractUrl, SlaveRemote):

    """scheme class for ssh:// urls

    interface to remote slave on master side
    implementing an ssh based proxy
    """

    def __init__(self, path):
        self.remote_addr, inner_url = sup(self, path,
                                          '^((?:%s@)?%s):(.+)' %
                                          tuple([r.pattern
                                                 for r in (UserRX, HostRX)]))
        self.inner_rsc = parse_url(inner_url)
        self.volume = inner_url[1:]

    @staticmethod
    def parse_ssh_address(self):
        m = re.match('([^@]+)@(.+)', self.remote_addr)
        if m:
            u, h = m.groups()
        else:
            u, h = syncdutils.getusername(), self.remote_addr
        self.remotehost = h
        return {'user': u, 'host': h}

    def canonical_path(self):
        rap = self.parse_ssh_address(self)
        remote_addr = '@'.join([rap['user'], gethostbyname(rap['host'])])
        return ':'.join([remote_addr, self.inner_rsc.get_url(canonical=True)])

    def can_connect_to(self, remote):
        """determine our position in the connectibility matrix"""
        return False

    def start_fd_client(self, *a, **opts):
        """customizations for client startup

        - be a no-op if we are to daemonize (client startup is deferred
          to post-daemon stage)
        - determine target url for rsync after consulting server
        """
        if opts.get('deferred'):
            return a
        sup(self, *a)
        ityp = type(self.inner_rsc)
        if ityp == FILE:
            slavepath = self.inner_rsc.path
        elif ityp == GLUSTER:
            slavepath = "/proc/%d/cwd" % self.server.pid()
        else:
            raise NotImplementedError
        self.slaveurl = ':'.join([self.remote_addr, slavepath])

    def connect_remote(self, go_daemon=None):
        """connect to inner slave url through outer ssh url

        Wrap the connecting utility in ssh.

        Much care is put into daemonizing: in that case
        ssh is started before daemonization, but
        RePCe client is to be created after that (as ssh
        interactive password auth would be defeated by
        a daemonized ssh, while client should be present
        only in the final process). In that case the action
        is taken apart to two parts, this method is ivoked
        once pre-daemon, once post-daemon. Use @go_daemon
        to deiced what part to perform.

        [NB. ATM gluster product does not makes use of interactive
        authentication.]
        """
        if go_daemon == 'done':
            return self.start_fd_client(*self.fd_pair)

        syncdutils.setup_ssh_ctl(tempfile.mkdtemp(prefix='gsyncd-aux-ssh-'),
                                 self.remote_addr,
                                 self.inner_rsc.url)

        deferred = go_daemon == 'postconn'
        ret = sup(self, gconf.ssh_command.split() +
                  ["-p", str(gconf.ssh_port)] +
                  gconf.ssh_ctl_args + [self.remote_addr],
                  slave=self.inner_rsc.url, deferred=deferred)

        if deferred:
            # send a message to peer so that we can wait for
            # the answer from which we know connection is
            # established and we can proceed with daemonization
            # (doing that too early robs the ssh passwd prompt...)
            # However, we'd better not start the RepceClient
            # before daemonization (that's not preserved properly
            # in daemon), we just do a an ad-hoc linear put/get.
            i, o = ret
            inf = os.fdopen(i)
            repce.send(o, None, '__repce_version__')
            select((inf,), (), ())
            repce.recv(inf)
            # hack hack hack: store a global reference to the file
            # to save it from getting GC'd which implies closing it
            gconf.permanent_handles.append(inf)
            self.fd_pair = (i, o)
            return 'should'

    def rsync(self, files, log_err=False):
        return sup(self, files, '-e',
                   " ".join(gconf.ssh_command.split() +
                            ["-p", str(gconf.ssh_port)] +
                            gconf.ssh_ctl_args),
                   *(gconf.rsync_ssh_options.split() + [self.slaveurl]),
                   log_err=log_err)

    def tarssh(self, files, log_err=False):
        return sup(self, files, self.slaveurl, log_err=log_err)
