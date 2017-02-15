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
import time
import stat
import json
import logging
import fcntl
import string
import errno
import tarfile
from errno import ENOENT, ENODATA, EEXIST, EACCES, EAGAIN, ESTALE
from threading import Condition, Lock
from datetime import datetime
from gconf import gconf
from syncdutils import Thread, GsyncdError, boolify, escape
from syncdutils import unescape, gauxpfx, md5hex, selfkill
from syncdutils import lstat, errno_wrap, FreeObject
from syncdutils import NoStimeAvailable, PartialHistoryAvailable

URXTIME = (-1, 0)

# Default rollover time set in changelog translator
# changelog rollover time is hardcoded here to avoid the
# xsync usage when crawling switch happens from history
# to changelog. If rollover time increased in translator
# then geo-rep can enter into xsync crawl after history
# crawl before starting live changelog crawl.
CHANGELOG_ROLLOVER_TIME = 15

# Max size of Changelogs to process per batch, Changelogs Processing is
# not limited by the number of changelogs but instead based on
# size of the changelog file, One sample changelog file size was 145408
# with ~1000 CREATE and ~1000 DATA. 5 such files in one batch is 727040
# If geo-rep worker crashes while processing a batch, it has to retry only
# that batch since stime will get updated after each batch.
MAX_CHANGELOG_BATCH_SIZE = 727040

# Utility functions to help us to get to closer proximity
# of the DRY principle (no, don't look for elevated or
# perspectivistic things here)


def _xtime_now():
    t = time.time()
    sec = int(t)
    nsec = int((t - sec) * 1000000)
    return (sec, nsec)


def _volinfo_hook_relax_foreign(self):
    volinfo_sys = self.get_sys_volinfo()
    fgn_vi = volinfo_sys[self.KFGN]
    if fgn_vi:
        expiry = fgn_vi['timeout'] - int(time.time()) + 1
        logging.info('foreign volume info found, waiting %d sec for expiry' %
                     expiry)
        time.sleep(expiry)
        volinfo_sys = self.get_sys_volinfo()
    return volinfo_sys


def edct(op, **ed):
    dct = {}
    dct['op'] = op
    for k in ed:
        if k == 'stat':
            st = ed[k]
            dst = dct['stat'] = {}
            if st:
                dst['uid'] = st.st_uid
                dst['gid'] = st.st_gid
                dst['mode'] = st.st_mode
                dst['atime'] = st.st_atime
                dst['mtime'] = st.st_mtime
        else:
            dct[k] = ed[k]
    return dct


# The API!

def gmaster_builder(excrawl=None):
    """produce the GMaster class variant corresponding
       to sync mode"""
    this = sys.modules[__name__]
    modemixin = gconf.special_sync_mode
    if not modemixin:
        modemixin = 'normal'
    changemixin = 'xsync' if gconf.change_detector == 'xsync' \
                  else excrawl or gconf.change_detector
    logging.debug('setting up %s change detection mode' % changemixin)
    modemixin = getattr(this, modemixin.capitalize() + 'Mixin')
    crawlmixin = getattr(this, 'GMaster' + changemixin.capitalize() + 'Mixin')
    sendmarkmixin = boolify(
        gconf.use_rsync_xattrs) and SendmarkRsyncMixin or SendmarkNormalMixin
    purgemixin = boolify(
        gconf.ignore_deletes) and PurgeNoopMixin or PurgeNormalMixin
    syncengine = boolify(gconf.use_tarssh) and TarSSHEngine or RsyncEngine

    class _GMaster(crawlmixin, modemixin, sendmarkmixin,
                   purgemixin, syncengine):
        pass
    return _GMaster


# Mixin classes that implement the data format
# and logic particularities of the certain
# sync modes

class NormalMixin(object):

    """normal geo-rep behavior"""

    minus_infinity = URXTIME

    # following staticmethods ideally would be
    # methods of an xtime object (in particular,
    # implementing the hooks needed for comparison
    # operators), but at this point we don't yet
    # have a dedicated xtime class

    @staticmethod
    def serialize_xtime(xt):
        return "%d.%d" % tuple(xt)

    @staticmethod
    def deserialize_xtime(xt):
        return tuple(int(x) for x in xt.split("."))

    @staticmethod
    def native_xtime(xt):
        return xt

    @staticmethod
    def xtime_geq(xt0, xt1):
        return xt0 >= xt1

    def make_xtime_opts(self, is_master, opts):
        if not 'create' in opts:
            opts['create'] = is_master
        if not 'default_xtime' in opts:
            opts['default_xtime'] = URXTIME

    def xtime_low(self, rsc, path, **opts):
        if rsc == self.master:
            xt = rsc.server.xtime(path, self.uuid)
        else:
            xt = rsc.server.stime(path, self.uuid)
            if isinstance(xt, int) and xt == ENODATA:
                xt = rsc.server.xtime(path, self.uuid)
                if not isinstance(xt, int):
                    self.slave.server.set_stime(path, self.uuid, xt)
        if isinstance(xt, int) and xt != ENODATA:
            return xt
        if xt == ENODATA or xt < self.volmark:
            if opts['create']:
                xt = _xtime_now()
                rsc.server.aggregated.set_xtime(path, self.uuid, xt)
            else:
                zero_zero = (0, 0)
                if xt != zero_zero:
                    xt = opts['default_xtime']
        return xt

    def keepalive_payload_hook(self, timo, gap):
        # first grab a reference as self.volinfo
        # can be changed in main thread
        vi = self.volinfo
        if vi:
            # then have a private copy which we can mod
            vi = vi.copy()
            vi['timeout'] = int(time.time()) + timo
        else:
            # send keep-alives more frequently to
            # avoid a delay in announcing our volume info
            # to slave if it becomes established in the
            # meantime
            gap = min(10, gap)
        return (vi, gap)

    def volinfo_hook(self):
        return self.get_sys_volinfo()

    def xtime_reversion_hook(self, path, xtl, xtr):
        if xtr > xtl:
            raise GsyncdError("timestamp corruption for " + path)

    def need_sync(self, e, xte, xtrd):
        return xte > xtrd

    def set_slave_xtime(self, path, mark):
        self.slave.server.set_stime(path, self.uuid, mark)
        # self.slave.server.set_xtime_remote(path, self.uuid, mark)


class PartialMixin(NormalMixin):

    """a variant tuned towards operation with a master
       that has partial info of the slave (brick typically)"""

    def xtime_reversion_hook(self, path, xtl, xtr):
        pass


class RecoverMixin(NormalMixin):

    """a variant that differs from normal in terms
       of ignoring non-indexed files"""

    @staticmethod
    def make_xtime_opts(is_master, opts):
        if not 'create' in opts:
            opts['create'] = False
        if not 'default_xtime' in opts:
            opts['default_xtime'] = URXTIME

    def keepalive_payload_hook(self, timo, gap):
        return (None, gap)

    def volinfo_hook(self):
        return _volinfo_hook_relax_foreign(self)

# Further mixins for certain tunable behaviors


class SendmarkNormalMixin(object):

    def sendmark_regular(self, *a, **kw):
        return self.sendmark(*a, **kw)


class SendmarkRsyncMixin(object):

    def sendmark_regular(self, *a, **kw):
        pass


class PurgeNormalMixin(object):

    def purge_missing(self, path, names):
        self.slave.server.purge(path, names)


class PurgeNoopMixin(object):

    def purge_missing(self, path, names):
        pass


class TarSSHEngine(object):

    """Sync engine that uses tar(1) piped over ssh(1)
       for data transfers. Good for lots of small files.
    """

    def a_syncdata(self, files):
        logging.debug('files: %s' % (files))

        for f in files:
            pb = self.syncer.add(f)

            def regjob(se, xte, pb):
                rv = pb.wait()
                if rv[0]:
                    logging.debug('synced ' + se)
                    return True
                else:
                    # stat check for file presence
                    st = lstat(se)
                    if isinstance(st, int):
                        # file got unlinked in the interim
                        self.unlinked_gfids.add(se)
                        return True

            self.add_job(self.FLAT_DIR_HIERARCHY, 'reg', regjob, f, None, pb)

    def syncdata_wait(self):
        if self.wait(self.FLAT_DIR_HIERARCHY, None):
            return True

    def syncdata(self, files):
        self.a_syncdata(files)
        self.syncdata_wait()


class RsyncEngine(object):

    """Sync engine that uses rsync(1) for data transfers"""

    def a_syncdata(self, files):
        logging.debug('files: %s' % (files))

        for f in files:
            logging.debug('candidate for syncing %s' % f)
            pb = self.syncer.add(f)

            def regjob(se, xte, pb):
                rv = pb.wait()
                if rv[0]:
                    logging.debug('synced ' + se)
                    return True
                else:
                    # stat to check if the file exist
                    st = lstat(se)
                    if isinstance(st, int):
                        # file got unlinked in the interim
                        self.unlinked_gfids.add(se)
                        return True

            self.add_job(self.FLAT_DIR_HIERARCHY, 'reg', regjob, f, None, pb)

    def syncdata_wait(self):
        if self.wait(self.FLAT_DIR_HIERARCHY, None):
            return True

    def syncdata(self, files):
        self.a_syncdata(files)
        self.syncdata_wait()


class GMasterCommon(object):

    """abstract class impementling master role"""

    KFGN = 0
    KNAT = 1

    def get_sys_volinfo(self):
        """query volume marks on fs root

        err out on multiple foreign masters
        """
        fgn_vis, nat_vi = (
            self.master.server.aggregated.foreign_volume_infos(),
            self.master.server.aggregated.native_volume_info())
        fgn_vi = None
        if fgn_vis:
            if len(fgn_vis) > 1:
                raise GsyncdError("cannot work with multiple foreign masters")
            fgn_vi = fgn_vis[0]
        return fgn_vi, nat_vi

    @property
    def uuid(self):
        if self.volinfo:
            return self.volinfo['uuid']

    @property
    def volmark(self):
        if self.volinfo:
            return self.volinfo['volume_mark']

    def get_entry_stime(self):
        data = self.slave.server.entry_stime(".", self.uuid)
        if isinstance(data, int):
            data = None
        return data

    def get_data_stime(self):
        data = self.slave.server.stime(".", self.uuid)
        if isinstance(data, int):
            data = None
        return data

    def xtime(self, path, *a, **opts):
        """get amended xtime

        as of amending, we can create missing xtime, or
        determine a valid value if what we get is expired
        (as of the volume mark expiry); way of amendig
        depends on @opts and on subject of query (master
        or slave).
        """
        if a:
            rsc = a[0]
        else:
            rsc = self.master
        self.make_xtime_opts(rsc == self.master, opts)
        return self.xtime_low(rsc, path, **opts)

    def __init__(self, master, slave):
        self.master = master
        self.slave = slave
        self.jobtab = {}
        if boolify(gconf.use_tarssh):
            self.syncer = Syncer(slave, self.slave.tarssh, [2])
        else:
            # partial transfer (cf. rsync(1)), that's normal
            self.syncer = Syncer(slave, self.slave.rsync, [23, 24])
        # crawls vs. turns:
        # - self.crawls is simply the number of crawl() invocations on root
        # - one turn is a maximal consecutive sequence of crawls so that each
        #   crawl in it detects a change to be synced
        # - self.turns is the number of turns since start
        # - self.total_turns is a limit so that if self.turns reaches it, then
        #   we exit (for diagnostic purposes)
        # so, eg., if the master fs changes unceasingly, self.turns will remain
        # 0.
        self.crawls = 0
        self.turns = 0
        self.total_turns = int(gconf.turns)
        self.crawl_start = datetime.now()
        self.lastreport = {'crawls': 0, 'turns': 0, 'time': 0}
        self.start = None
        self.change_seen = None
        # the actual volinfo we make use of
        self.volinfo = None
        self.terminate = False
        self.sleep_interval = 1
        self.unlinked_gfids = set()

    def init_keep_alive(cls):
        """start the keep-alive thread """
        timo = int(gconf.timeout or 0)
        if timo > 0:
            def keep_alive():
                while True:
                    vi, gap = cls.keepalive_payload_hook(timo, timo * 0.5)
                    cls.slave.server.keep_alive(vi)
                    time.sleep(gap)
            t = Thread(target=keep_alive)
            t.start()

    def mgmt_lock(self):

        """Take management volume lock """
        if gconf.mgmt_lock_fd:
            try:
                fcntl.lockf(gconf.mgmt_lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                if not gconf.active_earlier:
                    gconf.active_earlier = True
                    logging.info("Got lock : %s : Becoming ACTIVE"
                                 % gconf.local_path)
                return True
            except:
                ex = sys.exc_info()[1]
                if isinstance(ex, IOError) and ex.errno in (EACCES, EAGAIN):
                    if not gconf.passive_earlier:
                        gconf.passive_earlier = True
                        logging.info("Didn't get lock : %s : Becoming PASSIVE"
                                     % gconf.local_path)
                    return False
                raise

        fd = None
        bname = str(self.uuid) + "_" + str(gconf.slave_id) + "_subvol_" \
            + str(gconf.subvol_num) + ".lock"
        mgmt_lock_dir = os.path.join(gconf.meta_volume_mnt, "geo-rep")
        path = os.path.join(mgmt_lock_dir, bname)
        logging.debug("lock_file_path: %s" % path)
        try:
            fd = os.open(path, os.O_CREAT | os.O_RDWR)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno == ENOENT:
                logging.info("Creating geo-rep directory in meta volume...")
                try:
                    os.makedirs(mgmt_lock_dir)
                except OSError:
                    ex = sys.exc_info()[1]
                    if ex.errno == EEXIST:
                        pass
                    else:
                        raise
                fd = os.open(path, os.O_CREAT | os.O_RDWR)
            else:
                raise
        try:
            fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            # Save latest FD for future use
            gconf.mgmt_lock_fd = fd
        except:
            ex = sys.exc_info()[1]
            if isinstance(ex, IOError) and ex.errno in (EACCES, EAGAIN):
                # cannot grab, it's taken
                if not gconf.passive_earlier:
                    gconf.passive_earlier = True
                    logging.info("Didn't get lock : %s : Becoming PASSIVE"
                                 % gconf.local_path)
                gconf.mgmt_lock_fd = fd
                return False
            raise

        if not gconf.active_earlier:
            gconf.active_earlier = True
            logging.info("Got lock : %s : Becoming ACTIVE" % gconf.local_path)
        return True

    def should_crawl(self):
        if not boolify(gconf.use_meta_volume):
            return gconf.glusterd_uuid in self.master.server.node_uuid()

        if not os.path.ismount(gconf.meta_volume_mnt):
            logging.error("Meta-volume is not mounted. Worker Exiting...")
            sys.exit(1)
        return self.mgmt_lock()

    def register(self):
        self.register()

    def crawlwrap(self, oneshot=False, register_time=None):
        if oneshot:
            # it's important to do this during the oneshot crawl as
            # for a passive gsyncd (ie. in a replicate scenario)
            # the keepalive thread would keep the connection alive.
            self.init_keep_alive()

        # If crawlwrap is called when partial history available,
        # then it sets register_time which is the time when geo-rep
        # worker registerd to changelog consumption. Since nsec is
        # not considered in register time, their are chances of skipping
        # changes detection in xsync crawl. This limit will be reset when
        # crawlwrap is called again.
        self.live_changelog_start_time = None
        if register_time:
            self.live_changelog_start_time = (register_time, 0)

        # no need to maintain volinfo state machine.
        # in a cascading setup, each geo-replication session is
        # independent (ie. 'volume-mark' and 'xtime' are not
        # propogated). This is because the slave's xtime is now
        # stored on the master itself. 'volume-mark' just identifies
        # that we are in a cascading setup and need to enable
        # 'geo-replication.ignore-pid-check' option.
        volinfo_sys = self.volinfo_hook()
        self.volinfo = volinfo_sys[self.KNAT]
        inter_master = volinfo_sys[self.KFGN]
        logging.debug("%s master with volume id %s ..." %
                      (inter_master and "intermediate" or "primary",
                       self.uuid))
        gconf.configinterface.set('volume_id', self.uuid)
        if self.volinfo:
            if self.volinfo['retval']:
                logging.warn("master cluster's info may not be valid %d" %
                             self.volinfo['retval'])
        else:
            raise GsyncdError("master volinfo unavailable")
        self.lastreport['time'] = time.time()

        t0 = time.time()
        crawl = self.should_crawl()
        while not self.terminate:
            if self.start:
                logging.debug("... crawl #%d done, took %.6f seconds" %
                              (self.crawls, time.time() - self.start))
            self.start = time.time()
            should_display_info = self.start - self.lastreport['time'] >= 60
            if should_display_info:
                logging.debug("%d crawls, %d turns",
                              self.crawls - self.lastreport['crawls'],
                              self.turns - self.lastreport['turns'])
                self.lastreport.update(crawls=self.crawls,
                                       turns=self.turns,
                                       time=self.start)
            t1 = time.time()
            if int(t1 - t0) >= int(gconf.replica_failover_interval):
                crawl = self.should_crawl()
                t0 = t1
            self.update_worker_remote_node()
            if not crawl:
                self.status.set_passive()
                # bring up _this_ brick to the cluster stime
                # which is min of cluster (but max of the replicas)
                brick_stime = self.xtime('.', self.slave)
                cluster_stime = self.master.server.aggregated.stime_mnt(
                    '.', '.'.join([str(self.uuid), str(gconf.slave_id)]))
                logging.debug("Cluster stime: %s | Brick stime: %s" %
                              (repr(cluster_stime), repr(brick_stime)))
                if not isinstance(cluster_stime, int):
                    if brick_stime < cluster_stime:
                        self.slave.server.set_stime(
                            self.FLAT_DIR_HIERARCHY, self.uuid, cluster_stime)
                        # Purge all changelogs available in processing dir
                        # less than cluster_stime
                        proc_dir = os.path.join(self.tempdir,
                                                ".processing")

                        if os.path.exists(proc_dir):
                            to_purge = [f for f in os.listdir(proc_dir)
                                        if (f.startswith("CHANGELOG.") and
                                            int(f.split('.')[-1]) <
                                            cluster_stime[0])]
                            for f in to_purge:
                                os.remove(os.path.join(proc_dir, f))

                time.sleep(5)
                continue

            self.status.set_active()
            self.crawl()

            if oneshot:
                return
            time.sleep(self.sleep_interval)

    @staticmethod
    def humantime(*tpair):
        """format xtime-like (sec, nsec) pair to human readable format"""
        ts = datetime.fromtimestamp(float('.'.join(str(n) for n in tpair))).\
            strftime("%Y-%m-%d %H:%M:%S")
        if len(tpair) > 1:
            ts += '.' + str(tpair[1])
        return ts

    def _crawl_time_format(self, crawl_time):
        # Ex: 5 years, 4 days, 20:23:10
        years, days = divmod(crawl_time.days, 365.25)
        years = int(years)
        days = int(days)

        date = ""
        m, s = divmod(crawl_time.seconds, 60)
        h, m = divmod(m, 60)

        if years != 0:
            date += "%s %s " % (years, "year" if years == 1 else "years")
        if days != 0:
            date += "%s %s " % (days, "day" if days == 1 else "days")

        date += "%s:%s:%s" % (string.zfill(h, 2),
                              string.zfill(m, 2), string.zfill(s, 2))
        return date

    def add_job(self, path, label, job, *a, **kw):
        """insert @job function to job table at @path with @label"""
        if self.jobtab.get(path) is None:
            self.jobtab[path] = []
        self.jobtab[path].append((label, a, lambda: job(*a, **kw)))

    def add_failjob(self, path, label):
        """invoke .add_job with a job that does nothing just fails"""
        logging.debug('salvaged: ' + label)
        self.add_job(path, label, lambda: False)

    def wait(self, path, *args):
        """perform jobs registered for @path

        Reset jobtab entry for @path,
        determine success as the conjunction of
        success of all the jobs. In case of
        success, call .sendmark on @path
        """
        jobs = self.jobtab.pop(path, [])
        succeed = True
        for j in jobs:
            ret = j[-1]()
            if not ret:
                succeed = False
        if succeed and not args[0] is None:
            self.sendmark(path, *args)
        return succeed

    def sendmark(self, path, mark, adct=None):
        """update slave side xtime for @path to master side xtime

        also can send a setattr payload (see Server.setattr).
        """
        if adct:
            self.slave.server.setattr(path, adct)
        self.set_slave_xtime(path, mark)


class XCrawlMetadata(object):
    def __init__(self, st_uid, st_gid, st_mode, st_atime, st_mtime):
        self.st_uid = int(st_uid)
        self.st_gid = int(st_gid)
        self.st_mode = int(st_mode)
        self.st_atime = float(st_atime)
        self.st_mtime = float(st_mtime)


class GMasterChangelogMixin(GMasterCommon):

    """ changelog based change detection and syncing """

    # index for change type and entry
    IDX_START = 0
    IDX_END = 2
    UNLINK_ENTRY = 2

    POS_GFID = 0
    POS_TYPE = 1
    POS_ENTRY1 = -1

    TYPE_META = "M "
    TYPE_GFID = "D "
    TYPE_ENTRY = "E "

    # flat directory hierarchy for gfid based access
    FLAT_DIR_HIERARCHY = '.'

    CHANGELOG_CONN_RETRIES = 5

    def archive_and_purge_changelogs(self, changelogs):
        # Creates tar file instead of tar.gz, since changelogs will
        # be appended to existing tar. archive name is
        # archive_<YEAR><MONTH>.tar
        archive_name = "archive_%s.tar" % datetime.today().strftime(
            gconf.changelog_archive_format)

        try:
            tar = tarfile.open(os.path.join(self.processed_changelogs_dir,
                                            archive_name),
                               "a")
        except tarfile.ReadError:
            tar = tarfile.open(os.path.join(self.processed_changelogs_dir,
                                            archive_name),
                               "w")

        for f in changelogs:
            try:
                f = os.path.basename(f)
                tar.add(os.path.join(self.processed_changelogs_dir, f),
                        arcname=os.path.basename(f))
            except:
                exc = sys.exc_info()[1]
                if ((isinstance(exc, OSError) or
                     isinstance(exc, IOError)) and exc.errno == ENOENT):
                    continue
                else:
                    tar.close()
                    raise
        tar.close()

        for f in changelogs:
            try:
                f = os.path.basename(f)
                os.remove(os.path.join(self.processed_changelogs_dir, f))
            except OSError as e:
                if e.errno == errno.ENOENT:
                    continue
                else:
                    raise

    def fallback_xsync(self):
        logging.info('falling back to xsync mode')
        gconf.configinterface.set('change-detector', 'xsync')
        selfkill()

    def setup_working_dir(self):
        workdir = os.path.join(gconf.working_dir, md5hex(gconf.local_path))
        logging.debug('changelog working dir %s' % workdir)
        return workdir

    def log_failures(self, failures, entry_key, gfid_prefix, log_prefix):
        num_failures = 0
        for failure in failures:
            st = lstat(os.path.join(gfid_prefix, failure[0][entry_key]))
            if not isinstance(st, int):
                num_failures += 1
                logging.error('%s FAILED: %s' % (log_prefix,
                                                 repr(failure)))
                if failure[0]['op'] == 'MKDIR':
                    raise GsyncdError("The above directory failed to sync."
                                      " Please fix it to proceed further.")

        self.status.inc_value("failures", num_failures)

    def process_change(self, change, done, retry):
        pfx = gauxpfx()
        clist = []
        entries = []
        meta_gfid = set()
        datas = set()

        change_ts = change.split(".")[-1]

        # Ignore entry ops which are already processed in Changelog modes
        ignore_entry_ops = False
        entry_stime = None
        data_stime = None
        if self.name in ["live_changelog", "history_changelog"]:
            entry_stime = self.get_entry_stime()
            data_stime = self.get_data_stime()

        if entry_stime is not None and data_stime is not None:
            # if entry_stime is not None but data_stime > entry_stime
            # This situation is caused by the stime update of Passive worker
            # Consider data_stime in this case.
            if data_stime[0] > entry_stime[0]:
                entry_stime = data_stime

            # Compare the entry_stime with changelog file suffix
            # if changelog time is less than entry_stime then ignore
            if int(change_ts) <= entry_stime[0]:
                ignore_entry_ops = True

        try:
            f = open(change, "r")
            clist = f.readlines()
            f.close()
        except IOError:
            raise

        for e in clist:
            e = e.strip()
            et = e[self.IDX_START:self.IDX_END]   # entry type
            ec = e[self.IDX_END:].split(' ')      # rest of the bits

            # skip ENTRY operation if hot tier brick
            if self.name == 'live_changelog' or \
               self.name == 'history_changelog':
                if boolify(gconf.is_hottier) and et == self.TYPE_ENTRY:
                    logging.debug('skip ENTRY op: %s if hot tier brick'
                                  % (ec[self.POS_TYPE]))
                    continue

            # Data and Meta operations are decided while parsing
            # UNLINK/RMDIR/MKNOD except that case ignore all the other
            # entry ops if ignore_entry_ops is True.
            # UNLINK/RMDIR/MKNOD entry_ops are ignored in the end
            if ignore_entry_ops and et == self.TYPE_ENTRY and \
               ec[self.POS_TYPE] not in ["UNLINK", "RMDIR", "MKNOD"]:
                continue

            if et == self.TYPE_ENTRY:
                # extract information according to the type of
                # the entry operation. create(), mkdir() and mknod()
                # have mode, uid, gid information in the changelog
                # itself, so no need to stat()...
                ty = ec[self.POS_TYPE]

                # PARGFID/BNAME
                en = unescape(os.path.join(pfx, ec[self.POS_ENTRY1]))
                # GFID of the entry
                gfid = ec[self.POS_GFID]

                if ty in ['UNLINK', 'RMDIR']:
                    # The index of PARGFID/BNAME for UNLINK, RMDIR
                    # is no more the last index. It varies based on
                    # changelog.capture-del-path is enabled or not.
                    en = unescape(os.path.join(pfx, ec[self.UNLINK_ENTRY]))

                    # Remove from DATA list, so that rsync will
                    # not fail
                    pt = os.path.join(pfx, ec[0])
                    st = lstat(pt)
                    if pt in datas and isinstance(st, int):
                        # file got unlinked, May be historical Changelog
                        datas.remove(pt)

                    if not boolify(gconf.ignore_deletes):
                        if not ignore_entry_ops:
                            entries.append(edct(ty, gfid=gfid, entry=en))
                elif ty in ['CREATE', 'MKDIR', 'MKNOD']:
                    # Special case: record mknod as link
                    if ty in ['MKNOD']:
                        mode = int(ec[2])
                        if mode & 01000:
                                # Avoid stat'ing the file as it
                                # may be deleted in the interim
                                st = FreeObject(st_mode=int(ec[2]),
                                                st_uid=int(ec[3]),
                                                st_gid=int(ec[4]),
                                                st_atime=0,
                                                st_mtime=0)

                                # So, it may be deleted, but still we are
                                # append LINK? Because, the file will be
                                # CREATED if source not exists.
                                entries.append(edct('LINK', stat=st, entry=en,
                                               gfid=gfid))

                                # Here, we have the assumption that only
                                # tier-gfid.linkto causes this mknod. Add data
                                datas.add(os.path.join(pfx, ec[0]))
                                continue

                    # stat info. present in the changelog itself
                    entries.append(edct(ty, gfid=gfid, entry=en,
                                   mode=int(ec[2]),
                                   uid=int(ec[3]), gid=int(ec[4])))
                elif ty == "RENAME":
                    go = os.path.join(pfx, gfid)
                    st = lstat(go)
                    if isinstance(st, int):
                        st = {}

                    rl = None
                    if st and stat.S_ISLNK(st.st_mode):
                        rl = errno_wrap(os.readlink, [en], [ENOENT], [ESTALE])
                        if isinstance(rl, int):
                            rl = None

                    e1 = unescape(os.path.join(pfx, ec[self.POS_ENTRY1 - 1]))
                    entries.append(edct(ty, gfid=gfid, entry=e1, entry1=en,
                                        stat=st, link=rl))
                else:
                    # stat() to get mode and other information
                    go = os.path.join(pfx, gfid)
                    st = lstat(go)
                    if isinstance(st, int):
                        logging.debug('file %s got purged in the interim' % go)
                        continue

                    if ty == 'LINK':
                        entries.append(edct(ty, stat=st, entry=en, gfid=gfid))
                    elif ty == 'SYMLINK':
                        rl = errno_wrap(os.readlink, [en], [ENOENT], [ESTALE])
                        if isinstance(rl, int):
                            continue

                        entries.append(
                            edct(ty, stat=st, entry=en, gfid=gfid, link=rl))
                    else:
                        logging.warn('ignoring %s [op %s]' % (gfid, ty))
            elif et == self.TYPE_GFID:
                # If self.unlinked_gfids is available, then that means it is
                # retrying the changelog second time. Do not add the GFID's
                # to rsync job if failed previously but unlinked in master
                if self.unlinked_gfids and \
                   os.path.join(pfx, ec[0]) in self.unlinked_gfids:
                    logging.debug("ignoring data, since file purged interim")
                else:
                    datas.add(os.path.join(pfx, ec[0]))
            elif et == self.TYPE_META:
                if ec[1] == 'SETATTR':  # only setattr's for now...
                    if len(ec) == 5:
                        # In xsync crawl, we already have stat data
                        # avoid doing stat again
                        meta_gfid.add((os.path.join(pfx, ec[0]),
                                       XCrawlMetadata(st_uid=ec[2],
                                                      st_gid=ec[3],
                                                      st_mode=ec[4],
                                                      st_atime=ec[5],
                                                      st_mtime=ec[6])))
                    else:
                        meta_gfid.add((os.path.join(pfx, ec[0]), ))
                elif ec[1] == 'SETXATTR' or ec[1] == 'XATTROP' or \
                     ec[1] == 'FXATTROP':
                    # To sync xattr/acls use rsync/tar, --xattrs and --acls
                    # switch to rsync and tar
                    if not boolify(gconf.use_tarssh) and \
                       (boolify(gconf.sync_xattrs) or boolify(gconf.sync_acls)):
                        datas.add(os.path.join(pfx, ec[0]))
            else:
                logging.warn('got invalid changelog type: %s' % (et))
        logging.debug('entries: %s' % repr(entries))

        # Increment counters for Status
        self.status.inc_value("entry", len(entries))
        self.files_in_batch += len(datas)
        self.status.inc_value("data", len(datas))

        # sync namespace
        if entries and not ignore_entry_ops:
            # Increment counters for Status
            self.status.inc_value("entry", len(entries))

            failures = self.slave.server.entry_ops(entries)
            self.log_failures(failures, 'gfid', gauxpfx(), 'ENTRY')
            self.status.dec_value("entry", len(entries))

            # Update Entry stime in Brick Root only in case of Changelog mode
            if self.name in ["live_changelog", "history_changelog"]:
                entry_stime_to_update = (int(change_ts) - 1, 0)
                self.upd_entry_stime(entry_stime_to_update)
                self.status.set_field("last_synced_entry",
                                      entry_stime_to_update[0])

        if ignore_entry_ops:
            # Book keeping, to show in logs the range of Changelogs skipped
            self.num_skipped_entry_changelogs += 1
            if self.skipped_entry_changelogs_first is None:
                self.skipped_entry_changelogs_first = change_ts

            self.skipped_entry_changelogs_last = change_ts

        # sync metadata
        if meta_gfid:
            meta_entries = []
            for go in meta_gfid:
                if len(go) > 1:
                    st = go[1]
                else:
                    st = lstat(go[0])
                if isinstance(st, int):
                    logging.debug('file %s got purged in the interim' % go[0])
                    continue
                meta_entries.append(edct('META', go=go[0], stat=st))
            if meta_entries:
                self.status.inc_value("meta", len(entries))
                failures = self.slave.server.meta_ops(meta_entries)
                self.log_failures(failures, 'go', '', 'META')
                self.status.dec_value("meta", len(entries))

        # sync data
        if datas:
            self.a_syncdata(datas)
            self.datas_in_batch.update(datas)

    def process(self, changes, done=1):
        tries = 0
        retry = False
        self.unlinked_gfids = set()
        self.files_in_batch = 0
        self.datas_in_batch = set()
        # Error log disabled till the last round
        self.syncer.disable_errorlog()
        self.skipped_entry_changelogs_first = None
        self.skipped_entry_changelogs_last = None
        self.num_skipped_entry_changelogs = 0
        self.batch_start_time = time.time()

        while True:
            # first, fire all changelog transfers in parallel. entry and
            # metadata are performed synchronously, therefore in serial.
            # However at the end of each changelog, data is synchronized
            # with syncdata_async() - which means it is serial w.r.t
            # entries/metadata of that changelog but happens in parallel
            # with data of other changelogs.

            if retry:
                if tries == (int(gconf.max_rsync_retries) - 1):
                    # Enable Error logging if it is last retry
                    self.syncer.enable_errorlog()

                # Remove Unlinked GFIDs from Queue
                for unlinked_gfid in self.unlinked_gfids:
                    if unlinked_gfid in self.datas_in_batch:
                        self.datas_in_batch.remove(unlinked_gfid)

                # Retry only Sync. Do not retry entry ops
                if self.datas_in_batch:
                    self.a_syncdata(self.datas_in_batch)
            else:
                for change in changes:
                    logging.debug('processing change %s' % change)
                    self.process_change(change, done, retry)
                    if not retry:
                        # number of changelogs processed in the batch
                        self.turns += 1

            # Now we wait for all the data transfers fired off in the above
            # step to complete. Note that this is not ideal either. Ideally
            # we want to trigger the entry/meta-data transfer of the next
            # batch while waiting for the data transfer of the current batch
            # to finish.

            # Note that the reason to wait for the data transfer (vs doing it
            # completely in the background and call the changelog_done()
            # asynchronously) is because this waiting acts as a "backpressure"
            # and prevents a spiraling increase of wait stubs from consuming
            # unbounded memory and resources.

            # update the slave's time with the timestamp of the _last_
            # changelog file time suffix. Since, the changelog prefix time
            # is the time when the changelog was rolled over, introduce a
            # tolerance of 1 second to counter the small delta b/w the
            # marker update and gettimeofday().
            # NOTE: this is only for changelog mode, not xsync.

            # @change is the last changelog (therefore max time for this batch)
            if self.syncdata_wait():
                self.unlinked_gfids = set()
                if done:
                    xtl = (int(change.split('.')[-1]) - 1, 0)
                    self.upd_stime(xtl)
                    map(self.changelog_done_func, changes)
                    self.archive_and_purge_changelogs(changes)

                # Reset Data counter after sync
                self.status.dec_value("data", self.files_in_batch)
                self.files_in_batch = 0
                self.datas_in_batch = set()
                break

            # We do not know which changelog transfer failed, retry everything.
            retry = True
            tries += 1
            if tries == int(gconf.max_rsync_retries):
                logging.error('changelogs %s could not be processed '
                              'completely - moving on...' %
                              ' '.join(map(os.path.basename, changes)))

                # Reset data counter on failure
                self.status.dec_value("data", self.files_in_batch)
                self.files_in_batch = 0
                self.datas_in_batch = set()

                if done:
                    xtl = (int(change.split('.')[-1]) - 1, 0)
                    self.upd_stime(xtl)
                    map(self.changelog_done_func, changes)
                    self.archive_and_purge_changelogs(changes)
                break
            # it's either entry_ops() or Rsync that failed to do it's
            # job. Mostly it's entry_ops() [which currently has a problem
            # of failing to create an entry but failing to return an errno]
            # Therefore we do not know if it's either Rsync or the freaking
            # entry_ops() that failed... so we retry the _whole_ changelog
            # again.
            # TODO: remove entry retries when it's gets fixed.
            logging.warn('incomplete sync, retrying changelogs: %s' %
                         ' '.join(map(os.path.basename, changes)))

            # Reset the Data counter before Retry
            self.status.dec_value("data", self.files_in_batch)
            self.files_in_batch = 0
            time.sleep(0.5)

        # Log the Skipped Entry ops range if any
        if self.skipped_entry_changelogs_first is not None and \
           self.skipped_entry_changelogs_last is not None:
            logging.info("Skipping already processed entry "
                         "ops from CHANGELOG.{0} to CHANGELOG.{1} "
                         "Num: {2}".format(
                             self.skipped_entry_changelogs_first,
                             self.skipped_entry_changelogs_last,
                             self.num_skipped_entry_changelogs))

        # Log Current batch details
        if changes:
            logging.info(
                "{0} mode completed in {1:.4f} seconds "
                "({2} - {3} Num: {4}) stime: {5}, entry_stime: {6}".format(
                    self.name,
                    time.time() - self.batch_start_time,
                    changes[0].split("/")[-1],
                    changes[-1].split("/")[-1],
                    len(changes),
                    repr(self.get_data_stime()),
                    repr(self.get_entry_stime())))

    def upd_entry_stime(self, stime):
        self.slave.server.set_entry_stime(self.FLAT_DIR_HIERARCHY,
                                          self.uuid,
                                          stime)

    def upd_stime(self, stime, path=None):
        if not path:
            path = self.FLAT_DIR_HIERARCHY
        if not stime == URXTIME:
            self.sendmark(path, stime)

        # Update last_synced_time in status file based on stime
        # only update stime if stime xattr set to Brick root
        if path == self.FLAT_DIR_HIERARCHY:
            chkpt_time = gconf.configinterface.get_realtime(
                "checkpoint")
            checkpoint_time = 0
            if chkpt_time is not None:
                checkpoint_time = int(chkpt_time)

            self.status.set_last_synced(stime, checkpoint_time)

    def update_worker_remote_node(self):
        node = sys.argv[-1]
        node_data = node.split("@")
        node = node_data[-1]
        remote_node_ip = node.split(":")[0]
        self.status.set_slave_node(remote_node_ip)

    def changelogs_batch_process(self, changes):
        changelogs_batches = []
        current_size = 0
        for c in changes:
            si = os.lstat(c).st_size
            if (si + current_size) > MAX_CHANGELOG_BATCH_SIZE:
                # Create new batch if single Changelog file greater than
                # Max Size! or current batch size exceeds Max size
                changelogs_batches.append([c])
                current_size = si
            else:
                # Append to last batch, if No batches available Create one
                current_size += si
                if not changelogs_batches:
                    changelogs_batches.append([c])
                else:
                    changelogs_batches[-1].append(c)

        for batch in changelogs_batches:
            logging.debug('processing changes %s' % repr(batch))
            self.process(batch)

    def crawl(self):
        self.status.set_worker_crawl_status("Changelog Crawl")
        changes = []
        # get stime (from the brick) and purge changelogs
        # that are _historical_ to that time.
        data_stime = self.get_data_stime()

        self.changelog_agent.scan()
        self.crawls += 1
        changes = self.changelog_agent.getchanges()
        if changes:
            if data_stime:
                logging.info("slave's time: %s" % repr(data_stime))
                processed = [x for x in changes
                             if int(x.split('.')[-1]) < data_stime[0]]
                for pr in processed:
                    logging.info(
                        'skipping already processed change: %s...' %
                        os.path.basename(pr))
                    self.changelog_done_func(pr)
                    changes.remove(pr)
                self.archive_and_purge_changelogs(processed)

        self.changelogs_batch_process(changes)

    def register(self, register_time, changelog_agent, status):
        self.changelog_agent = changelog_agent
        self.sleep_interval = int(gconf.change_interval)
        self.changelog_done_func = self.changelog_agent.done
        self.tempdir = self.setup_working_dir()
        self.processed_changelogs_dir = os.path.join(self.tempdir,
                                                     ".processed")
        self.name = "live_changelog"
        self.status = status


class GMasterChangeloghistoryMixin(GMasterChangelogMixin):
    def register(self, register_time, changelog_agent, status):
        self.changelog_agent = changelog_agent
        self.changelog_register_time = register_time
        self.history_crawl_start_time = register_time
        self.changelog_done_func = self.changelog_agent.history_done
        self.history_turns = 0
        self.tempdir = self.setup_working_dir()
        self.processed_changelogs_dir = os.path.join(self.tempdir,
                                                     ".history/.processed")
        self.name = "history_changelog"
        self.status = status

    def crawl(self):
        self.history_turns += 1
        self.status.set_worker_crawl_status("History Crawl")
        data_stime = self.get_data_stime()

        end_time = int(time.time())
        logging.info('starting history crawl... turns: %s, stime: %s, '
                     'etime: %s, entry_stime: %s'
                     % (self.history_turns, repr(data_stime),
                        repr(end_time), self.get_entry_stime()))

        if not data_stime or data_stime == URXTIME:
            raise NoStimeAvailable()

        # Changelogs backend path is hardcoded as
        # <BRICK_PATH>/.glusterfs/changelogs, if user configured to different
        # location then consuming history will not work(Known issue as of now)
        changelog_path = os.path.join(gconf.local_path,
                                      ".glusterfs/changelogs")
        ret, actual_end = self.changelog_agent.history(
            changelog_path,
            data_stime[0],
            end_time,
            int(gconf.sync_jobs))

        # scan followed by getchanges till scan returns zero.
        # history_scan() is blocking call, till it gets the number
        # of changelogs to process. Returns zero when no changelogs
        # to be processed. returns positive value as number of changelogs
        # to be processed, which will be fetched using
        # history_getchanges()
        while self.changelog_agent.history_scan() > 0:
            self.crawls += 1

            changes = self.changelog_agent.history_getchanges()
            if changes:
                if data_stime:
                    logging.info("slave's time: %s" % repr(data_stime))
                    processed = [x for x in changes
                                 if int(x.split('.')[-1]) < data_stime[0]]
                    for pr in processed:
                        logging.info('skipping already processed change: '
                                     '%s...' % os.path.basename(pr))
                        self.changelog_done_func(pr)
                        changes.remove(pr)

            self.changelogs_batch_process(changes)

        history_turn_time = int(time.time()) - self.history_crawl_start_time

        logging.info('finished history crawl syncing, endtime: %s, '
                     'stime: %s, entry_stime: %s'
                     % (actual_end, repr(self.get_data_stime()),
                        self.get_entry_stime()))

        # If TS returned from history_changelog is < register_time
        # then FS crawl may be required, since history is only available
        # till TS returned from history_changelog
        if actual_end < self.changelog_register_time:
            if self.history_turns < 2:
                sleep_time = 1
                if history_turn_time < CHANGELOG_ROLLOVER_TIME:
                    sleep_time = CHANGELOG_ROLLOVER_TIME - history_turn_time
                time.sleep(sleep_time)
                self.history_crawl_start_time = int(time.time())
                self.crawl()
            else:
                # This exeption will be catched in resource.py and
                # fallback to xsync for the small gap.
                raise PartialHistoryAvailable(str(actual_end))


class GMasterXsyncMixin(GMasterChangelogMixin):

    """
    This crawl needs to be xtime based (as of now
    it's not. this is because we generate CHANGELOG
    file during each crawl which is then processed
    by process_change()).
    For now it's used as a one-shot initial sync
    mechanism and only syncs directories, regular
    files, hardlinks and symlinks.
    """

    XSYNC_MAX_ENTRIES = 1 << 13

    def register(self, register_time=None, changelog_agent=None, status=None):
        self.status = status
        self.counter = 0
        self.comlist = []
        self.stimes = []
        self.sleep_interval = 60
        self.tempdir = self.setup_working_dir()
        logging.info('Working dir: %s' % self.tempdir)
        self.tempdir = os.path.join(self.tempdir, 'xsync')
        self.processed_changelogs_dir = self.tempdir
        self.name = "xsync"
        try:
            os.makedirs(self.tempdir)
        except OSError:
            ex = sys.exc_info()[1]
            if ex.errno == EEXIST and os.path.isdir(self.tempdir):
                pass
            else:
                raise

    def crawl(self):
        """
        event dispatcher thread

        this thread dispatches either changelog or synchronizes stime.
        additionally terminates itself on receiving a 'finale' event
        """
        def Xsyncer():
            self.Xcrawl()
        t = Thread(target=Xsyncer)
        t.start()
        logging.info('starting hybrid crawl..., stime: %s'
                     % repr(self.get_data_stime()))
        self.status.set_worker_crawl_status("Hybrid Crawl")
        while True:
            try:
                item = self.comlist.pop(0)
                if item[0] == 'finale':
                    logging.info('finished hybrid crawl syncing, stime: %s'
                                 % repr(self.get_data_stime()))
                    break
                elif item[0] == 'xsync':
                    logging.info('processing xsync changelog %s' % (item[1]))
                    self.process([item[1]], 0)
                    self.archive_and_purge_changelogs([item[1]])
                elif item[0] == 'stime':
                    logging.debug('setting slave time: %s' % repr(item[1]))
                    self.upd_stime(item[1][1], item[1][0])
                else:
                    logging.warn('unknown tuple in comlist (%s)' % repr(item))
            except IndexError:
                time.sleep(1)

    def write_entry_change(self, prefix, data=[]):
        if not getattr(self, "fh", None):
            self.open()

        self.fh.write("%s %s\n" % (prefix, ' '.join(data)))

    def open(self):
        try:
            self.xsync_change = os.path.join(
                self.tempdir, 'XSYNC-CHANGELOG.' + str(int(time.time())))
            self.fh = open(self.xsync_change, 'w')
        except IOError:
            raise

    def close(self):
        if getattr(self, "fh", None):
            self.fh.flush()
            os.fsync(self.fh.fileno())
            self.fh.close()
            self.fh = None

    def fname(self):
        return self.xsync_change

    def put(self, mark, item):
        self.comlist.append((mark, item))

    def sync_xsync(self, last):
        """schedule a processing of changelog"""
        self.close()
        if self.counter > 0:
            self.put('xsync', self.fname())
        self.counter = 0
        if not last:
            time.sleep(1)  # make sure changelogs are 1 second apart

    def sync_stime(self, stime=None, last=False):
        """schedule a stime synchronization"""
        if stime:
            self.put('stime', stime)
        if last:
            self.put('finale', None)

    def sync_done(self, stime=[], last=False):
        self.sync_xsync(last)
        if stime:
            # Send last as True only for last stime entry
            for st in stime[:-1]:
                self.sync_stime(st, False)

            if stime and stime[-1]:
                self.sync_stime(stime[-1], last)

    def is_sticky(self, path, mo):
        """check for DHTs linkto sticky bit file"""
        sticky = False
        if mo & 01000:
            sticky = self.master.server.linkto_check(path)
        return sticky

    def Xcrawl(self, path='.', xtr_root=None):
        """
        generate a CHANGELOG file consumable by process_change.

        slave's xtime (stime) is _cached_ for comparisons across
        the filesystem tree, but set after directory synchronization.
        """
        if path == '.':
            self.crawls += 1
        if not xtr_root:
            # get the root stime and use it for all comparisons
            xtr_root = self.xtime('.', self.slave)
            if isinstance(xtr_root, int):
                if xtr_root != ENOENT:
                    logging.warn("slave cluster not returning the "
                                 "correct xtime for root (%d)" % xtr_root)
                xtr_root = self.minus_infinity
        xtl = self.xtime(path)
        if isinstance(xtl, int):
            logging.warn("master cluster's xtime not found")
        xtr = self.xtime(path, self.slave)
        if isinstance(xtr, int):
            if xtr != ENOENT:
                logging.warn("slave cluster not returning the "
                             "correct xtime for %s (%d)" % (path, xtr))
            xtr = self.minus_infinity
        xtr = max(xtr, xtr_root)
        zero_zero = (0, 0)
        if xtr_root == zero_zero:
            xtr = self.minus_infinity
        if not self.need_sync(path, xtl, xtr):
            if path == '.':
                self.sync_done([(path, xtl)], True)
            return
        self.xtime_reversion_hook(path, xtl, xtr)
        logging.debug("entering " + path)
        dem = self.master.server.entries(path)
        pargfid = self.master.server.gfid(path)
        if isinstance(pargfid, int):
            logging.warn('skipping directory %s' % (path))
        for e in dem:
            bname = e
            e = os.path.join(path, e)
            xte = self.xtime(e)
            if isinstance(xte, int):
                logging.warn("irregular xtime for %s: %s" %
                             (e, errno.errorcode[xte]))
                continue
            if not self.need_sync(e, xte, xtr):
                continue
            st = self.master.server.lstat(e)
            if isinstance(st, int):
                logging.warn('%s got purged in the interim ...' % e)
                continue
            if self.is_sticky(e, st.st_mode):
                logging.debug('ignoring sticky bit file %s' % e)
                continue
            gfid = self.master.server.gfid(e)
            if isinstance(gfid, int):
                logging.warn('skipping entry %s..' % e)
                continue
            mo = st.st_mode
            self.counter += 1 if ((stat.S_ISDIR(mo) or
                                   stat.S_ISLNK(mo) or
                                   stat.S_ISREG(mo))) else 0
            if self.counter == self.XSYNC_MAX_ENTRIES:
                self.sync_done(self.stimes, False)
                self.stimes = []
            if stat.S_ISDIR(mo):
                self.write_entry_change("E", [gfid, 'MKDIR', str(mo),
                    str(0), str(0), escape(os.path.join(pargfid, bname))])
                self.write_entry_change("M", [gfid, "SETATTR", str(st.st_uid),
                                              str(st.st_gid), str(st.st_mode),
                                              str(st.st_atime),
                                              str(st.st_mtime)])
                self.Xcrawl(e, xtr_root)
                stime_to_update = xte
                # Live Changelog Start time indicates that from that time
                # onwards Live changelogs are available. If we update stime
                # greater than live_changelog_start time then Geo-rep will
                # skip those changelogs as already processed. But Xsync
                # actually failed to sync the deletes and Renames. Update
                # stime as min(Live_changelogs_time, Actual_stime) When it
                # switches to Changelog mode, it syncs Deletes and Renames.
                if self.live_changelog_start_time:
                    stime_to_update = min(self.live_changelog_start_time, xte)
                self.stimes.append((e, stime_to_update))
            elif stat.S_ISLNK(mo):
                self.write_entry_change(
                    "E", [gfid, 'SYMLINK', escape(os.path.join(pargfid,
                                                               bname))])
            elif stat.S_ISREG(mo):
                nlink = st.st_nlink
                nlink -= 1  # fixup backend stat link count
                # if a file has a hardlink, create a Changelog entry as
                # 'LINK' so the slave side will decide if to create the
                # new entry, or to create link.
                if nlink == 1:
                    self.write_entry_change("E",
                                            [gfid, 'MKNOD', str(mo),
                                             str(0), str(0),
                                             escape(os.path.join(
                                                 pargfid, bname))])
                else:
                    self.write_entry_change(
                        "E", [gfid, 'LINK', escape(os.path.join(pargfid,
                                                                bname))])
                self.write_entry_change("D", [gfid])
        if path == '.':
            stime_to_update = xtl
            if self.live_changelog_start_time:
                stime_to_update = min(self.live_changelog_start_time, xtl)
            self.stimes.append((path, stime_to_update))
            self.sync_done(self.stimes, True)


class BoxClosedErr(Exception):
    pass


class PostBox(list):

    """synchronized collection for storing things thought of as "requests" """

    def __init__(self, *a):
        list.__init__(self, *a)
        # too bad Python stdlib does not have read/write locks...
        # it would suffivce to grab the lock in .append as reader, in .close as
        # writer
        self.lever = Condition()
        self.open = True
        self.done = False

    def wait(self):
        """wait on requests to be processed"""
        self.lever.acquire()
        if not self.done:
            self.lever.wait()
        self.lever.release()
        return self.result

    def wakeup(self, data):
        """wake up requestors with the result"""
        self.result = data
        self.lever.acquire()
        self.done = True
        self.lever.notifyAll()
        self.lever.release()

    def append(self, e):
        """post a request"""
        self.lever.acquire()
        if not self.open:
            raise BoxClosedErr
        list.append(self, e)
        self.lever.release()

    def close(self):
        """prohibit the posting of further requests"""
        self.lever.acquire()
        self.open = False
        self.lever.release()


class Syncer(object):

    """a staged queue to relay rsync requests to rsync workers

    By "staged queue" its meant that when a consumer comes to the
    queue, it takes _all_ entries, leaving the queue empty.
    (I don't know if there is an official term for this pattern.)

    The queue uses a PostBox to accumulate incoming items.
    When a consumer (rsync worker) comes, a new PostBox is
    set up and the old one is passed on to the consumer.

    Instead of the simplistic scheme of having one big lock
    which synchronizes both the addition of new items and
    PostBox exchanges, use a separate lock to arbitrate consumers,
    and rely on PostBox's synchronization mechanisms take
    care about additions.

    There is a corner case racy situation, producers vs. consumers,
    which is not handled by this scheme: namely, when the PostBox
    exchange occurs in between being passed to the producer for posting
    and the post placement. But that's what Postbox.close is for:
    such a posting will find the PostBox closed, in which case
    the producer can re-try posting against the actual PostBox of
    the queue.

    To aid accumlation of items in the PostBoxen before grabbed
    by an rsync worker, the worker goes to sleep a bit after
    each completed syncjob.
    """

    def __init__(self, slave, sync_engine, resilient_errnos=[]):
        """spawn worker threads"""
        self.log_err = False
        self.slave = slave
        self.lock = Lock()
        self.pb = PostBox()
        self.sync_engine = sync_engine
        self.errnos_ok = resilient_errnos
        for i in range(int(gconf.sync_jobs)):
            t = Thread(target=self.syncjob)
            t.start()

    def syncjob(self):
        """the life of a worker"""
        while True:
            pb = None
            while True:
                self.lock.acquire()
                if self.pb:
                    pb, self.pb = self.pb, PostBox()
                self.lock.release()
                if pb:
                    break
                time.sleep(0.5)
            pb.close()
            po = self.sync_engine(pb, self.log_err)
            if po.returncode == 0:
                ret = (True, 0)
            elif po.returncode in self.errnos_ok:
                ret = (False, po.returncode)
            else:
                po.errfail()
            pb.wakeup(ret)

    def add(self, e):
        while True:
            pb = self.pb
            try:
                pb.append(e)
                return pb
            except BoxClosedErr:
                pass

    def enable_errorlog(self):
        self.log_err = True

    def disable_errorlog(self):
        self.log_err = False
