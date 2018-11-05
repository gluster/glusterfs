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
import fcntl
import types
import struct
import logging
import tempfile
import subprocess
from errno import EEXIST, ENOENT, ENODATA, ENOTDIR, ELOOP, EACCES
from errno import EISDIR, ENOTEMPTY, ESTALE, EINVAL, EBUSY, EPERM
import errno

from rconf import rconf
import gsyncdconfig as gconf

import repce
from repce import RepceServer, RepceClient
from master import gmaster_builder
import syncdutils
from syncdutils import GsyncdError, select, privileged, funcode
from syncdutils import umask, entry2pb, gauxpfx, errno_wrap, lstat
from syncdutils import NoStimeAvailable, PartialHistoryAvailable
from syncdutils import ChangelogException, ChangelogHistoryNotAvailable
from syncdutils import get_changelog_log_level, get_rsync_version
from syncdutils import CHANGELOG_AGENT_CLIENT_VERSION
from syncdutils import GX_GFID_CANONICAL_LEN
from gsyncdstatus import GeorepStatus
from syncdutils import lf, Popen, sup
from syncdutils import Xattr, matching_disk_gfid, get_gfid_from_mnt
from syncdutils import unshare_propagation_supported, get_slv_dir_path


ENOTSUP = getattr(errno, 'ENOTSUP', 'EOPNOTSUPP')

slv_volume = None
slv_host = None


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

    # for backend gfid fetch, do not use GX_NSPACE_PFX
    GFID_XATTR = 'trusted.gfid'
    GFID_FMTSTR = "!" + "B" * 16

    local_path = ''

    @classmethod
    def _fmt_mknod(cls, l):
        return "!II%dsI%dsIII" % (GX_GFID_CANONICAL_LEN, l + 1)

    @classmethod
    def _fmt_mkdir(cls, l):
        return "!II%dsI%dsII" % (GX_GFID_CANONICAL_LEN, l + 1)

    @classmethod
    def _fmt_symlink(cls, l1, l2):
        return "!II%dsI%ds%ds" % (GX_GFID_CANONICAL_LEN, l1 + 1, l2 + 1)

    def _pathguard(f):
        """decorator method that checks
        the path argument of the decorated
        functions to make sure it does not
        point out of the managed tree
        """

        fc = funcode(f)
        pi = list(fc.co_varnames).index('path')

        def ff(*args):
            path = args[pi]
            ps = path.split('/')
            if path[0] == '/' or '..' in ps:
                raise ValueError('unsafe path')
            args = list(args)
            args[pi] = os.path.join(args[0].local_path, path)
            return f(*args)
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
        buf = errno_wrap(Xattr.lgetxattr, [path, cls.GFID_XATTR, 16],
                         [ENOENT], [ESTALE, ENODATA])
        if buf == ENOENT:
            return buf
        else:
            m = re.match('(.{8})(.{4})(.{4})(.{4})(.{12})', "".join(
                ['%02x' % x for x in struct.unpack(cls.GFID_FMTSTR, buf)]))
            return '-'.join(m.groups())

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

        def entry_purge(op, entry, gfid, e, uid, gid):
            # This is an extremely racy code and needs to be fixed ASAP.
            # The GFID check here is to be sure that the pargfid/bname
            # to be purged is the GFID gotten from the changelog.
            # (a stat(changelog_gfid) would also be valid here)
            # The race here is between the GFID check and the purge.

            # If the entry or the gfid of the file to be deleted is not present
            # on slave, we can ignore the unlink/rmdir
            if isinstance(lstat(entry), int) or \
               isinstance(lstat(os.path.join(pfx, gfid)), int):
                return

            if not matching_disk_gfid(gfid, entry):
                collect_failure(e, EEXIST, uid, gid)
                return

            if op == 'UNLINK':
                er = errno_wrap(os.unlink, [entry], [ENOENT, ESTALE], [EBUSY])
                # EISDIR is safe error, ignore. This can only happen when
                # unlink is sent from master while fixing gfid conflicts.
                if er != EISDIR:
                    return er

            elif op == 'RMDIR':
                er = errno_wrap(os.rmdir, [entry], [ENOENT, ESTALE,
                                                    ENOTEMPTY], [EBUSY])
                if er == ENOTEMPTY:
                    return er

        def collect_failure(e, cmd_ret, uid, gid, dst=False):
            slv_entry_info = {}
            slv_entry_info['gfid_mismatch'] = False
            slv_entry_info['name_mismatch'] = False
            slv_entry_info['dst'] = dst
            slv_entry_info['slave_isdir'] = False
            slv_entry_info['slave_name'] = None
            slv_entry_info['slave_gfid'] = None
            # We do this for failing fops on Slave
            # Master should be logging this
            if cmd_ret is None:
                return False

            if e.get("stat", {}):
                # Copy actual UID/GID value back to entry stat
                e['stat']['uid'] = uid
                e['stat']['gid'] = gid

            if cmd_ret == EEXIST:
                if dst:
                    en = e['entry1']
                else:
                    en = e['entry']
                disk_gfid = get_gfid_from_mnt(en)
                if isinstance(disk_gfid, basestring) and \
                   e['gfid'] != disk_gfid:
                    slv_entry_info['gfid_mismatch'] = True
                    st = lstat(en)
                    if not isinstance(st, int):
                        if st and stat.S_ISDIR(st.st_mode):
                            slv_entry_info['slave_isdir'] = True
                            dir_name = get_slv_dir_path(slv_host, slv_volume,
                                                        disk_gfid)
                            slv_entry_info['slave_name'] = dir_name
                        else:
                            slv_entry_info['slave_isdir'] = False
                    slv_entry_info['slave_gfid'] = disk_gfid
                    failures.append((e, cmd_ret, slv_entry_info))
                else:
                    return False
            else:
                failures.append((e, cmd_ret, slv_entry_info))

            return True

        failures = []

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
            names = errno_wrap(os.listdir, [path], [ENOENT], [ESTALE, ENOTSUP])
            if isinstance(names, int):
                return

            for name in names:
                fullname = os.path.join(path, name)
                if not matching_disk_gfid(gfid, entry):
                    return
                er = errno_wrap(os.remove, [fullname], [ENOENT, ESTALE,
                                                        EISDIR], [EBUSY])

                if er == EISDIR:
                    recursive_rmdir(gfid, entry, fullname)

            if not matching_disk_gfid(gfid, entry):
                return

            errno_wrap(os.rmdir, [path], [ENOENT, ESTALE], [EBUSY])

        def rename_with_disk_gfid_confirmation(gfid, entry, en, uid, gid):
            if not matching_disk_gfid(gfid, entry):
                logging.error(lf("RENAME ignored: source entry does not match "
                                 "with on-disk gfid",
                                 source=entry,
                                 gfid=gfid,
                                 disk_gfid=get_gfid_from_mnt(entry),
                                 target=en))
                collect_failure(e, EEXIST, uid, gid)
                return

            cmd_ret = errno_wrap(os.rename,
                                 [entry, en],
                                 [ENOENT, EEXIST], [ESTALE, EBUSY])
            collect_failure(e, cmd_ret, uid, gid)

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
                er = entry_purge(op, entry, gfid, e, uid, gid)
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
                            logging.warn(lf("Recursive remove failed",
                                            gfid=gfid,
                                            pgfid=pg,
                                            bname=bname,
                                            error=os.strerror(er1)))
                    else:
                        logging.warn(lf("Failed to remove",
                                        gfid=gfid,
                                        pgfid=pg,
                                        bname=bname,
                                        error=os.strerror(er)))
            elif op in ['CREATE', 'MKNOD']:
                slink = os.path.join(pfx, gfid)
                st = lstat(slink)
                # don't create multiple entries with same gfid
                if isinstance(st, int):
                    blob = entry_pack_reg(
                        gfid, bname, e['mode'], e['uid'], e['gid'])
                # Self healed hardlinks are recorded as MKNOD.
                # So if the gfid already exists, it should be
                # processed as hard link not mknod.
                elif op in ['MKNOD']:
                    cmd_ret = errno_wrap(os.link,
                                         [slink, entry],
                                         [ENOENT, EEXIST], [ESTALE])
                    collect_failure(e, cmd_ret, uid, gid)
            elif op == 'MKDIR':
                en = e['entry']
                slink = os.path.join(pfx, gfid)
                st = lstat(slink)
                # don't create multiple entries with same gfid
                if isinstance(st, int):
                    blob = entry_pack_mkdir(
                        gfid, bname, e['mode'], e['uid'], e['gid'])
                elif (isinstance(lstat(en), int) or
                      not matching_disk_gfid(gfid, en)):
                    # If gfid of a directory exists on slave but path based
                    # create is getting EEXIST. This means the directory is
                    # renamed in master but recorded as MKDIR during hybrid
                    # crawl. Get the directory path by reading the backend
                    # symlink and trying to rename to new name as said by
                    # master.
                    logging.info(lf("Special case: rename on mkdir",
                                    gfid=gfid, entry=repr(entry)))
                    src_entry = get_slv_dir_path(slv_host, slv_volume, gfid)
                    if src_entry is not None and src_entry != entry:
                        slv_entry_info = {}
                        slv_entry_info['gfid_mismatch'] = False
                        slv_entry_info['name_mismatch'] = True
                        slv_entry_info['dst'] = False
                        slv_entry_info['slave_isdir'] = True
                        slv_entry_info['slave_gfid'] = gfid
                        slv_entry_info['slave_entry'] = src_entry

                        failures.append((e, EEXIST, slv_entry_info))
            elif op == 'LINK':
                slink = os.path.join(pfx, gfid)
                st = lstat(slink)
                if isinstance(st, int):
                    (pg, bname) = entry2pb(entry)
                    if stat.S_ISREG(e['stat']['mode']):
                        blob = entry_pack_reg_stat(gfid, bname, e['stat'])
                    elif stat.S_ISLNK(e['stat']['mode']):
                        blob = entry_pack_symlink(gfid, bname, e['link'],
                                                  e['stat'])
                else:
                    cmd_ret = errno_wrap(os.link,
                                         [slink, entry],
                                         [ENOENT, EEXIST], [ESTALE])
                    collect_failure(e, cmd_ret, uid, gid)
            elif op == 'SYMLINK':
                en = e['entry']
                st = lstat(entry)
                if isinstance(st, int):
                    blob = entry_pack_symlink(gfid, bname, e['link'],
                                              e['stat'])
                elif not matching_disk_gfid(gfid, en):
                    collect_failure(e, EEXIST, uid, gid)
            elif op == 'RENAME':
                en = e['entry1']
                # The matching disk gfid check validates two things
                #  1. Validates name is present, return false otherwise
                #  2. Validates gfid is same, returns false otherwise
                # So both validations are necessary to decide src doesn't
                # exist. We can't rely on only gfid stat as hardlink could
                # be present and we can't rely only on name as name could
                # exist with differnt gfid.
                if not matching_disk_gfid(gfid, entry):
                    if e['stat'] and not stat.S_ISDIR(e['stat']['mode']):
                        if stat.S_ISLNK(e['stat']['mode']) and \
                           e['link'] is not None:
                            st1 = lstat(en)
                            if isinstance(st1, int):
                                (pg, bname) = entry2pb(en)
                                blob = entry_pack_symlink(gfid, bname,
                                                          e['link'], e['stat'])
                            elif not matching_disk_gfid(gfid, en):
                                collect_failure(e, EEXIST, uid, gid, True)
                        else:
                            slink = os.path.join(pfx, gfid)
                            st = lstat(slink)
                            # don't create multiple entries with same gfid
                            if isinstance(st, int):
                                (pg, bname) = entry2pb(en)
                                blob = entry_pack_reg_stat(gfid, bname,
                                                           e['stat'])
                            else:
                                cmd_ret = errno_wrap(os.link, [slink, en],
                                                    [ENOENT, EEXIST], [ESTALE])
                                collect_failure(e, cmd_ret, uid, gid)
                else:
                    st = lstat(entry)
                    st1 = lstat(en)
                    if isinstance(st1, int):
                        rename_with_disk_gfid_confirmation(gfid, entry, en,
                                                           uid, gid)
                    else:
                        if st.st_ino == st1.st_ino:
                            # we have a hard link, we can now unlink source
                            try:
                                errno_wrap(os.unlink, [entry],
                                           [ENOENT, ESTALE], [EBUSY])
                            except OSError as e:
                                if e.errno == EISDIR:
                                    try:
                                        errno_wrap(os.rmdir, [entry],
                                                   [ENOENT, ESTALE], [EBUSY])
                                    except OSError as e:
                                        if e.errno == ENOTEMPTY:
                                            logging.error(
                                                lf("Directory Rename failed. "
                                                   "Both Old and New"
                                                   " directories exists",
                                                   old=entry,
                                                   new=en))
                                        else:
                                            raise
                                else:
                                    raise
                        elif not matching_disk_gfid(gfid, en):
                            collect_failure(e, EEXIST, uid, gid, True)
                        else:
                            rename_with_disk_gfid_confirmation(gfid, entry, en,
                                                               uid, gid)
            if blob:
                cmd_ret = errno_wrap(Xattr.lsetxattr,
                                     [pg, 'glusterfs.gfid.newfile', blob],
                                     [EEXIST, ENOENT],
                                     [ESTALE, EINVAL, EBUSY])
                collect_failure(e, cmd_ret, uid, gid)

                # If UID/GID is different than zero that means we are trying
                # create Entry with different UID/GID. Create Entry with
                # UID:0 and GID:0, and then call chown to set UID/GID
                if uid != 0 or gid != 0:
                    path = os.path.join(pfx, gfid)
                    cmd_ret = errno_wrap(os.lchown, [path, uid, gid], [ENOENT],
                                         [ESTALE, EINVAL])
                    collect_failure(e, cmd_ret, uid, gid)

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
            # Linux doesn't support chmod on symlink itself.
            # It is always applied to the target file. So
            # changelog would record target file's gfid
            # and we are good. But 'chown' is supported on
            # symlink file. So changelog would record symlink
            # gfid in such cases. Since we do 'chown' 'chmod'
            # 'utime' for each gfid recorded for metadata, and
            # we know from changelog the metadata is on symlink's
            # gfid or target file's gfid, we should be doing
            # 'lchown' 'lchmod' 'utime with no-deference' blindly.
            # But since 'lchmod' and 'utime with no de-reference' is
            # not supported in python3, we have to rely on 'chmod'
            # and 'utime with de-reference'. Hence avoiding 'chmod'
            # and 'utime' if it's symlink file.

            is_symlink = False
            cmd_ret = errno_wrap(os.lchown, [go, uid, gid], [ENOENT],
                                 [ESTALE, EINVAL])
            if isinstance(cmd_ret, int):
                continue

            is_symlink = os.path.islink(go)

            if not is_symlink:
                cmd_ret = errno_wrap(os.chmod, [go, mode],
                                     [ENOENT, EACCES, EPERM], [ESTALE, EINVAL])
                if isinstance(cmd_ret, int):
                    failures.append((e, cmd_ret, "chmod"))

                cmd_ret = errno_wrap(os.utime, [go, (atime, mtime)],
                                     [ENOENT, EACCES, EPERM], [ESTALE, EINVAL])
                if isinstance(cmd_ret, int):
                    failures.append((e, cmd_ret, "utime"))
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


class Mounter(object):

    """Abstract base class for mounter backends"""

    def __init__(self, params):
        self.params = params
        self.mntpt = None
        self.umount_cmd = []

    @classmethod
    def get_glusterprog(cls):
        gluster_cmd_dir = gconf.get("gluster-command-dir")
        if rconf.args.subcmd == "slave":
            gluster_cmd_dir = gconf.get("slave-gluster-command-dir")
        return os.path.join(gluster_cmd_dir, cls.glusterprog)

    def umount_l(self, d):
        """perform lazy umount"""
        po = Popen(self.make_umount_argv(d), stderr=subprocess.PIPE)
        po.wait()
        return po

    @classmethod
    def make_umount_argv(cls, d):
        raise NotImplementedError

    def make_mount_argv(self, label=None):
        raise NotImplementedError

    def cleanup_mntpt(self, *a):
        pass

    def handle_mounter(self, po):
        po.wait()

    def inhibit(self, label):
        """inhibit a gluster filesystem

        Mount glusterfs over a temporary mountpoint,
        change into the mount, and lazy unmount the
        filesystem.
        """
        mpi, mpo = os.pipe()
        mh = Popen.fork()
        if mh:
            # Parent
            os.close(mpi)
            fcntl.fcntl(mpo, fcntl.F_SETFD, fcntl.FD_CLOEXEC)
            d = None
            margv = self.make_mount_argv(label)
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
            tlim = rconf.starttime + gconf.get("connection-timeout")
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
                logging.warn(lf('stale mount possibly left behind',
                                path=d))
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

                    umount_master = False
                    umount_slave = False
                    if rconf.args.subcmd == "worker" \
                       and not unshare_propagation_supported() \
                       and not gconf.get("access-mount"):
                        umount_master = True
                    if rconf.args.subcmd == "slave" \
                       and not gconf.get("slave-access-mount"):
                        umount_slave = True

                    if mounted and (umount_master or umount_slave):
                        po = self.umount_l(mntpt)
                        po.terminate_geterr(fail_on_err=False)
                        if po.returncode != 0:
                            po.errlog()
                            rv = po.returncode
                        logging.debug("Lazy umount done: %s" % mntpt)
                    if umount_master or umount_slave:
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

    def make_mount_argv(self, label=None):
        self.mntpt = tempfile.mkdtemp(prefix='gsyncd-aux-mount-')
        rconf.mount_point = self.mntpt
        return [self.get_glusterprog()] + \
            ['--' + p for p in self.params] + [self.mntpt]

    def cleanup_mntpt(self, mntpt=None):
        if not mntpt:
            mntpt = self.mntpt
        errno_wrap(os.rmdir, [mntpt], [ENOENT, EBUSY])


class MountbrokerMounter(Mounter):

    """mounter backend using the mountbroker gluster service"""

    mountkw = {'stderr': subprocess.PIPE, 'stdout': subprocess.PIPE}
    glusterprog = 'gluster'

    @classmethod
    def make_cli_argv(cls):
        return [cls.get_glusterprog()] + ['--remote-host=localhost'] + \
            gconf.get("gluster-cli-options").split() + ['system::']

    @classmethod
    def make_umount_argv(cls, d):
        return cls.make_cli_argv() + ['umount', d, 'lazy']

    def make_mount_argv(self, label):
        return self.make_cli_argv() + \
            ['mount', label, 'user-map-root=' +
                syncdutils.getusername()] + self.params

    def handle_mounter(self, po):
        self.mntpt = po.stdout.readline()[:-1]
        rconf.mount_point = self.mntpt
        rconf.mountbroker = True
        self.umount_cmd = self.make_cli_argv() + ['umount']
        rconf.mbr_umount_cmd = self.umount_cmd
        po.stdout.close()
        sup(self, po)
        if po.returncode != 0:
            # if cli terminated with error due to being
            # refused by glusterd, what it put
            # out on stdout is a diagnostic message
            logging.error(lf('glusterd answered', mnt=self.mntpt))


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


class GLUSTER(object):

    """scheme class for gluster:// urls

    can be used to represent a gluster slave server
    on slave side, or interface to a remote gluster
    slave on master side, or to represent master
    (slave-ish features come from the mixins, master
    functionality is outsourced to GMaster from master)
    """
    server = GLUSTERServer

    def __init__(self, host, volume):
        self.path = "%s:%s" % (host, volume)
        self.host = host
        self.volume = volume

        global slv_volume
        global slv_host
        slv_volume = self.volume
        slv_host = self.host

    def connect(self):
        """inhibit the resource beyond

        Choose mounting backend (direct or mountbroker),
        set up glusterfs parameters and perform the mount
        with given backend
        """

        logging.info("Mounting gluster volume locally...")
        t0 = time.time()
        label = gconf.get('mountbroker', None)
        if not label and not privileged():
            label = syncdutils.getusername()
        mounter = label and MountbrokerMounter or DirectMounter

        log_file = gconf.get("gluster-log-file")
        if rconf.args.subcmd == "slave":
            log_file = gconf.get("slave-gluster-log-file")

        log_level = gconf.get("gluster-log-level")
        if rconf.args.subcmd == "slave":
            log_level = gconf.get("slave-gluster-log-level")

        params = gconf.get("gluster-params").split() + \
            ['log-level=' + log_level] + \
            ['log-file=' + log_file, 'volfile-server=' + self.host] + \
            ['volfile-id=' + self.volume, 'client-pid=-1']

        self.mounter = mounter(params)
        self.mounter.inhibit(label)
        logging.info(lf("Mounted gluster volume",
                        duration="%.4f" % (time.time() - t0)))

    def gmaster_instantiate_tuple(self, slave):
        """return a tuple of the 'one shot' and the 'main crawl'
        class instance"""
        return (gmaster_builder('xsync')(self, slave),
                gmaster_builder()(self, slave),
                gmaster_builder('changeloghistory')(self, slave))

    def service_loop(self, slave=None):
        """enter service loop

        - if slave given, instantiate GMaster and
          pass control to that instance, which implements
          master behavior
        - else do that's what's inherited
        """
        if rconf.args.subcmd == "slave":
            if gconf.get("use-rsync-xattrs") and not privileged():
                raise GsyncdError(
                    "using rsync for extended attributes is not supported")

            repce = RepceServer(
                self.server, sys.stdin, sys.stdout, gconf.get("sync-jobs"))
            t = syncdutils.Thread(target=lambda: (repce.service_loop(),
                                                  syncdutils.finalize()))
            t.start()
            logging.info("slave listening")
            if gconf.get("slave-timeout") and gconf.get("slave-timeout") > 0:
                while True:
                    lp = self.server.last_keep_alive
                    time.sleep(gconf.get("slave-timeout"))
                    if lp == self.server.last_keep_alive:
                        logging.info(
                            lf("connection inactive, stopping",
                               timeout=gconf.get("slave-timeout")))
                        break
            else:
                select((), (), ())

            return

        class brickserver(Server):
            local_path = rconf.args.local_path
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

        # define {,set_}xtime in slave, thus preempting
        # the call to remote, so that it takes data from
        # the local brick
        slave.server.xtime = types.MethodType(
            lambda _self, path, uuid: (
                brickserver.xtime(path,
                                  uuid + '.' + rconf.args.slave_id)
            ),
            slave.server)
        slave.server.stime = types.MethodType(
            lambda _self, path, uuid: (
                brickserver.stime(path,
                                  uuid + '.' + rconf.args.slave_id)
            ),
            slave.server)
        slave.server.entry_stime = types.MethodType(
            lambda _self, path, uuid: (
                brickserver.entry_stime(
                    path,
                    uuid + '.' + rconf.args.slave_id)
            ),
            slave.server)
        slave.server.set_stime = types.MethodType(
            lambda _self, path, uuid, mark: (
                brickserver.set_stime(path,
                                      uuid + '.' + rconf.args.slave_id,
                                      mark)
            ),
            slave.server)
        slave.server.set_entry_stime = types.MethodType(
            lambda _self, path, uuid, mark: (
                brickserver.set_entry_stime(
                    path,
                    uuid + '.' + rconf.args.slave_id,
                    mark)
            ),
            slave.server)

        (g1, g2, g3) = self.gmaster_instantiate_tuple(slave)
        g1.master.server = brickserver
        g2.master.server = brickserver
        g3.master.server = brickserver

        # bad bad bad: bad way to do things like this
        # need to make this elegant
        # register the crawlers and start crawling
        # g1 ==> Xsync, g2 ==> config.change_detector(changelog by default)
        # g3 ==> changelog History
        (inf, ouf, ra, wa) = rconf.args.rpc_fd.split(',')
        changelog_agent = RepceClient(int(inf), int(ouf))

        status = GeorepStatus(gconf.get("state-file"),
                              rconf.args.local_node,
                              rconf.args.local_path,
                              rconf.args.local_node_id,
                              rconf.args.master,
                              rconf.args.slave)
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
            if gconf.get("change-detector") != 'xsync':
                # register with the changelog library
                # 9 == log level (DEBUG)
                # 5 == connection retries
                changelog_agent.init()
                changelog_agent.register(rconf.args.local_path,
                                         workdir,
                                         gconf.get("changelog-log-file"),
                                         get_changelog_log_level(
                                             gconf.get("changelog-log-level")),
                                         g2.CHANGELOG_CONN_RETRIES)

            register_time = int(time.time())
            g2.register(register_time, changelog_agent, status)
            g3.register(register_time, changelog_agent, status)
        except ChangelogException as e:
            logging.error(lf("Changelog register failed", error=e))
            sys.exit(1)

        g1.register(status=status)
        logging.info(lf("Register time",
                        time=register_time))
        # oneshot: Try to use changelog history api, if not
        # available switch to FS crawl
        # Note: if config.change_detector is xsync then
        # it will not use changelog history api
        try:
            g3.crawlwrap(oneshot=True)
        except PartialHistoryAvailable as e:
            logging.info(lf('Partial history available, using xsync crawl'
                            ' after consuming history',
                            till=e))
            g1.crawlwrap(oneshot=True, register_time=register_time)
        except ChangelogHistoryNotAvailable:
            logging.info('Changelog history not available, using xsync')
            g1.crawlwrap(oneshot=True, register_time=register_time)
        except NoStimeAvailable:
            logging.info('No stime available, using xsync crawl')
            g1.crawlwrap(oneshot=True, register_time=register_time)
        except ChangelogException as e:
            logging.error(lf("Changelog History Crawl failed",
                             error=e))
            sys.exit(1)

        try:
            g2.crawlwrap()
        except ChangelogException as e:
            logging.error(lf("Changelog crawl failed", error=e))
            sys.exit(1)


class SSH(object):

    """scheme class for ssh:// urls

    interface to remote slave on master side
    implementing an ssh based proxy
    """

    def __init__(self, host, volume):
        self.remote_addr = host
        self.volume = volume

    @staticmethod
    def parse_ssh_address(self):
        m = re.match('([^@]+)@(.+)', self.remote_addr)
        if m:
            u, h = m.groups()
        else:
            u, h = syncdutils.getusername(), self.remote_addr
        self.remotehost = h
        return {'user': u, 'host': h}

    def start_fd_client(self, i, o):
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
        slavepath = "/proc/%d/cwd" % self.server.pid()
        self.slaveurl = ':'.join([self.remote_addr, slavepath])

    def connect_remote(self):
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
        syncdutils.setup_ssh_ctl(tempfile.mkdtemp(prefix='gsyncd-aux-ssh-'),
                                 self.remote_addr,
                                 self.volume)

        logging.info("Initializing SSH connection between master and slave...")
        t0 = time.time()

        extra_opts = []
        remote_gsyncd = gconf.get("remote-gsyncd")
        if remote_gsyncd == "":
            remote_gsyncd = "/nonexistent/gsyncd"

        if gconf.get("use-rsync-xattrs"):
            extra_opts.append('--use-rsync-xattrs')

        args_to_slave = [gconf.get("ssh-command")] + \
            gconf.get("ssh-options").split() + \
            ["-p", str(gconf.get("ssh-port"))] + \
            rconf.ssh_ctl_args + [self.remote_addr] + \
            [remote_gsyncd, "slave"] + \
            extra_opts + \
            [rconf.args.master, rconf.args.slave] + \
            [
                '--master-node', rconf.args.local_node,
                '--master-node-id', rconf.args.local_node_id,
                '--master-brick', rconf.args.local_path,
                '--local-node', rconf.args.resource_remote,
                '--local-node-id', rconf.args.resource_remote_id] + \
            [
                # Add all config arguments here, slave gsyncd will not use
                # config file in slave side, so all overridding options should
                # be sent as arguments
                '--slave-timeout', str(gconf.get("slave-timeout")),
                '--slave-log-level', gconf.get("slave-log-level"),
                '--slave-gluster-log-level',
                gconf.get("slave-gluster-log-level"),
                '--slave-gluster-command-dir',
                gconf.get("slave-gluster-command-dir")]

        if gconf.get("slave-access-mount"):
            args_to_slave.append('--slave-access-mount')

        if rconf.args.debug:
            args_to_slave.append('--debug')

        po = Popen(args_to_slave,
                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE)
        rconf.transport = po
        self.start_fd_client(po.stdout, po.stdin)
        logging.info(lf("SSH connection between master and slave established.",
                        duration="%.4f" % (time.time() - t0)))

    def rsync(self, files, *args, **kw):
        """invoke rsync"""
        if not files:
            raise GsyncdError("no files to sync")
        logging.debug("files: " + ", ".join(files))

        extra_rsync_flags = []
        # Performance flag, --ignore-missing-args, if rsync version is
        # greater than 3.1.0 then include this flag.
        if gconf.get("rsync-opt-ignore-missing-args") and \
           get_rsync_version(gconf.get("rsync-command")) >= "3.1.0":
            extra_rsync_flags = ["--ignore-missing-args"]

        rsync_ssh_opts = [gconf.get("ssh-command")] + \
            gconf.get("ssh-options").split() + \
            ["-p", str(gconf.get("ssh-port"))] + \
            rconf.ssh_ctl_args + \
            gconf.get("rsync-ssh-options").split()

        argv = [
            gconf.get("rsync-command"),
            '-aR0',
            '--inplace',
            '--files-from=-',
            '--super',
            '--stats',
            '--numeric-ids',
            '--no-implied-dirs'
        ]

        if gconf.get("rsync-opt-existing"):
            argv += ["--existing"]

        if gconf.get("sync-xattrs"):
            argv += ['--xattrs']

        if gconf.get("sync-acls"):
            argv += ['--acls']

        argv = argv + \
            gconf.get("rsync-options").split() + \
            extra_rsync_flags + ['.'] + \
            ["-e", " ".join(rsync_ssh_opts)] + \
            [self.slaveurl]

        log_rsync_performance = gconf.getr("log-rsync-performance", False)

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
                logging.error(lf("SYNC Error",
                                 sync_engine="Rsync",
                                 error=errline))

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
            logging.info(lf("rsync performance",
                            data=", ".join(rsync_msg)))

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
        ssh_cmd = gconf.get("ssh-command-tar").split() + \
            gconf.get("ssh-options-tar").split() + \
            ["-p", str(gconf.get("ssh-port"))] + \
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
                logging.error(lf("SYNC Error",
                                 sync_engine="Tarssh",
                                 error=errline))

        return p1
