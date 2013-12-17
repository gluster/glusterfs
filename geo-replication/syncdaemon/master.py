import os
import sys
import time
import stat
import random
import signal
import json
import logging
import socket
import string
import errno
from shutil import copyfileobj
from errno import ENOENT, ENODATA, EPIPE, EEXIST, errorcode
from threading import currentThread, Condition, Lock
from datetime import datetime
from libcxattr import Xattr

from gconf import gconf
from tempfile import mkdtemp, NamedTemporaryFile
from syncdutils import FreeObject, Thread, GsyncdError, boolify, escape, \
                       unescape, select, gauxpfx, md5hex, selfkill, entry2pb, \
                       lstat, errno_wrap, update_file

URXTIME = (-1, 0)

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
        logging.info('foreign volume info found, waiting %d sec for expiry' % \
                     expiry)
        time.sleep(expiry)
        volinfo_sys = self.get_sys_volinfo()
    return volinfo_sys


# The API!

def gmaster_builder(excrawl=None):
    """produce the GMaster class variant corresponding
       to sync mode"""
    this = sys.modules[__name__]
    modemixin = gconf.special_sync_mode
    if not modemixin:
        modemixin = 'normal'
    changemixin = isinstance(excrawl, str) and excrawl or gconf.change_detector
    logging.info('setting up %s change detection mode' % changemixin)
    modemixin = getattr(this, modemixin.capitalize() + 'Mixin')
    crawlmixin = getattr(this, 'GMaster' + changemixin.capitalize() + 'Mixin')
    sendmarkmixin = boolify(gconf.use_rsync_xattrs) and SendmarkRsyncMixin or SendmarkNormalMixin
    purgemixin = boolify(gconf.ignore_deletes) and PurgeNoopMixin or PurgeNormalMixin
    syncengine = boolify(gconf.use_tarssh) and TarSSHEngine or RsyncEngine
    class _GMaster(crawlmixin, modemixin, sendmarkmixin, purgemixin, syncengine):
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
        if isinstance(xt, int) and xt != ENODATA:
            return xt
        if xt == ENODATA or xt < self.volmark:
            if opts['create']:
                xt = _xtime_now()
                rsc.server.aggregated.set_xtime(path, self.uuid, xt)
            else:
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
        self.slave.server.set_xtime_remote(path, self.uuid, mark)

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
                        return True
                    logging.warn('tar+ssh: %s [errcode: %d]' % (se, rv[1]))
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
                    if rv[1] in [23, 24]:
                        # stat to check if the file exist
                        st = lstat(se)
                        if isinstance(st, int):
                            # file got unlinked in the interim
                            return True
                    logging.warn('Rsync: %s [errcode: %d]' % (se, rv[1]))
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
        fgn_vis, nat_vi = self.master.server.aggregated.foreign_volume_infos(), \
                          self.master.server.aggregated.native_volume_info()
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

    def get_initial_crawl_data(self):
        # while persisting only 'files_syncd' is non-zero, rest of
        # the stats are nulls. lets keep it that way in case they
        # are needed to be used some day...
        default_data = {'files_syncd': 0,
                        'files_remaining': 0,
                        'bytes_remaining': 0,
                        'purges_remaining': 0,
                        'total_files_skipped': 0}
        if getattr(gconf, 'state_detail_file', None):
            try:
                with open(gconf.state_detail_file, 'r+') as f:
                    loaded_data= json.load(f)
                    diff_data = set(default_data) - set (loaded_data)
                    if len(diff_data):
                        for i in diff_data:
                            loaded_data[i] = default_data[i]
                    return loaded_data
            except (IOError):
                ex = sys.exc_info()[1]
                logging.warn ('Creating new gconf.state_detail_file.')
                # Create file with initial data
                try:
                    with open(gconf.state_detail_file, 'wb') as f:
                        json.dump(default_data, f)
                    return default_data
                except:
                    raise
        return default_data

    def update_crawl_data(self):
        if getattr(gconf, 'state_detail_file', None):
            try:
                same_dir = os.path.dirname(gconf.state_detail_file)
                with NamedTemporaryFile(dir=same_dir, delete=False) as tmp:
                    json.dump(self.total_crawl_stats, tmp)
                    tmp.flush()
                    os.fsync(tmp.fileno())
                    os.rename(tmp.name, gconf.state_detail_file)
            except (IOError, OSError):
                raise

    def __init__(self, master, slave):
        self.master = master
        self.slave = slave
        self.jobtab = {}
        if boolify(gconf.use_tarssh):
            logging.info("using 'tar over ssh' as the sync engine")
            self.syncer = Syncer(slave, self.slave.tarssh)
        else:
            logging.info("using 'rsync' as the sync engine")
            # partial transfer (cf. rsync(1)), that's normal
            self.syncer = Syncer(slave, self.slave.rsync, [23, 24])
        # crawls vs. turns:
        # - self.crawls is simply the number of crawl() invocations on root
        # - one turn is a maximal consecutive sequence of crawls so that each
        #   crawl in it detects a change to be synced
        # - self.turns is the number of turns since start
        # - self.total_turns is a limit so that if self.turns reaches it, then
        #   we exit (for diagnostic purposes)
        # so, eg., if the master fs changes unceasingly, self.turns will remain 0.
        self.crawls = 0
        self.turns = 0
        self.total_turns = int(gconf.turns)
        self.crawl_start = datetime.now()
        self.lastreport = {'crawls': 0, 'turns': 0, 'time': 0}
        self.total_crawl_stats = None
        self.start = None
        self.change_seen = None
        # the actual volinfo we make use of
        self.volinfo = None
        self.terminate = False
        self.sleep_interval = 1
        self.checkpoint_thread = None
        self.current_files_skipped_count = 0
        self.skipped_gfid_list = []

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

    def should_crawl(cls):
        return (gconf.glusterd_uuid in cls.master.server.node_uuid())

    def register(self):
        self.register()

    def crawlwrap(self, oneshot=False):
        if oneshot:
            # it's important to do this during the oneshot crawl as
            # for a passive gsyncd (ie. in a replicate scenario)
            # the keepalive thread would keep the connection alive.
            self.init_keep_alive()

        # no need to maintain volinfo state machine.
        # in a cascading setup, each geo-replication session is
        # independent (ie. 'volume-mark' and 'xtime' are not
        # propogated). This is beacuse the slave's xtime is now
        # stored on the master itself. 'volume-mark' just identifies
        # that we are in a cascading setup and need to enable
        # 'geo-replication.ignore-pid-check' option.
        volinfo_sys = self.volinfo_hook()
        self.volinfo = volinfo_sys[self.KNAT]
        inter_master = volinfo_sys[self.KFGN]
        logging.info("%s master with volume id %s ..." % \
                         (inter_master and "intermediate" or "primary",
                          self.uuid))
        gconf.configinterface.set('volume_id', self.uuid)
        if self.volinfo:
            if self.volinfo['retval']:
                logging.warn("master cluster's info may not be valid %d" % \
                             self.volinfo['retval'])
            self.start_checkpoint_thread()
        else:
            raise GsyncdError("master volinfo unavailable")
	self.total_crawl_stats = self.get_initial_crawl_data()
        self.lastreport['time'] = time.time()
        logging.info('crawl interval: %d seconds' % self.sleep_interval)

        t0 = time.time()
        crawl = self.should_crawl()
        while not self.terminate:
            if self.start:
                logging.debug("... crawl #%d done, took %.6f seconds" % \
                              (self.crawls, time.time() - self.start))
            self.start = time.time()
            should_display_info = self.start - self.lastreport['time'] >= 60
            if should_display_info:
                logging.info("%d crawls, %d turns",
                             self.crawls - self.lastreport['crawls'],
                             self.turns - self.lastreport['turns'])
                self.lastreport.update(crawls = self.crawls,
                                       turns = self.turns,
                                       time = self.start)
            t1 = time.time()
            if int(t1 - t0) >= 60: #lets hardcode this check to 60 seconds
                crawl = self.should_crawl()
                t0 = t1
            self.update_worker_remote_node()
            if not crawl:
                self.update_worker_health("Passive")
                # bring up _this_ brick to the cluster stime
                # which is min of cluster (but max of the replicas)
                brick_stime = self.xtime('.', self.slave)
                cluster_stime = self.master.server.aggregated.stime_mnt('.', '.'.join([str(self.uuid), str(gconf.slave_id)]))
                logging.debug("Cluster stime: %s | Brick stime: %s" % (repr(cluster_stime), repr(brick_stime)))
                if not isinstance(cluster_stime, int):
                    if brick_stime < cluster_stime:
                        self.slave.server.set_stime(self.FLAT_DIR_HIERARCHY, self.uuid, cluster_stime)
                time.sleep(5)
                continue
            self.update_worker_health("Active")
            self.crawl()
            if oneshot:
                return
            time.sleep(self.sleep_interval)

    @classmethod
    def _checkpt_param(cls, chkpt, prm, xtimish=True):
        """use config backend to lookup a parameter belonging to
           checkpoint @chkpt"""
        cprm = gconf.configinterface.get_realtime('checkpoint_' + prm)
        if not cprm:
            return
        chkpt_mapped, val = cprm.split(':', 1)
        if unescape(chkpt_mapped) != chkpt:
            return
        if xtimish:
            val = cls.deserialize_xtime(val)
        return val

    @classmethod
    def _set_checkpt_param(cls, chkpt, prm, val, xtimish=True):
        """use config backend to store a parameter associated
           with checkpoint @chkpt"""
        if xtimish:
            val = cls.serialize_xtime(val)
        gconf.configinterface.set('checkpoint_' + prm, "%s:%s" % (escape(chkpt), val))

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

        date=""
        m, s = divmod(crawl_time.seconds, 60)
        h, m = divmod(m, 60)

        if years != 0:
            date += "%s %s " % (years, "year" if years == 1 else "years")
        if days != 0:
            date += "%s %s " % (days, "day" if days == 1 else "days")

        date += "%s:%s:%s" % (string.zfill(h, 2), string.zfill(m, 2), string.zfill(s, 2))
        return date

    def checkpt_service(self, chan, chkpt):
        """checkpoint service loop

        monitor and verify checkpoint status for @chkpt, and listen
        for incoming requests for whom we serve a pretty-formatted
        status report"""
        while True:
            chkpt = gconf.configinterface.get_realtime("checkpoint")
            if not chkpt:
                gconf.configinterface.delete("checkpoint_completed")
                gconf.configinterface.delete("checkpoint_target")
                # dummy loop for the case when there is no checkpt set
                select([chan], [], [])
                conn, _ = chan.accept()
                conn.send('\0')
                conn.close()
                continue

            checkpt_tgt = self._checkpt_param(chkpt, 'target')
            if not checkpt_tgt:
                checkpt_tgt = self.xtime('.')
                if isinstance(checkpt_tgt, int):
                    raise GsyncdError("master root directory is unaccessible (%s)",
                                      os.strerror(checkpt_tgt))
                self._set_checkpt_param(chkpt, 'target', checkpt_tgt)
            logging.debug("checkpoint target %s has been determined for checkpoint %s" % \
                          (repr(checkpt_tgt), chkpt))

            # check if the label is 'now'
            chkpt_lbl = chkpt
            try:
                x1,x2 = chkpt.split(':')
                if x1 == 'now':
                    chkpt_lbl = "as of " + self.humantime(x2)
            except:
                pass
            completed = self._checkpt_param(chkpt, 'completed', xtimish=False)
            if completed:
                completed = tuple(int(x) for x in completed.split('.'))
            s,_,_ = select([chan], [], [], (not completed) and 5 or None)
            # either request made and we re-check to not
            # give back stale data, or we still hunting for completion
            if self.native_xtime(checkpt_tgt) and self.native_xtime(checkpt_tgt) < self.volmark:
                # indexing has been reset since setting the checkpoint
                status = "is invalid"
            else:
                xtr = self.xtime('.', self.slave)
                if isinstance(xtr, int):
                    raise GsyncdError("slave root directory is unaccessible (%s)",
                                      os.strerror(xtr))
                ncompleted = self.xtime_geq(xtr, checkpt_tgt)
                if completed and not ncompleted: # stale data
                    logging.warn("completion time %s for checkpoint %s became stale" % \
                                 (self.humantime(*completed), chkpt))
                    completed = None
                    gconf.configinterface.delete('checkpoint_completed')
                if ncompleted and not completed: # just reaching completion
                    completed = "%.6f" % time.time()
                    self._set_checkpt_param(chkpt, 'completed', completed, xtimish=False)
                    completed = tuple(int(x) for x in completed.split('.'))
                    logging.info("checkpoint %s completed" % chkpt)
                status = completed and \
                  "completed at " + self.humantime(completed[0]) or \
                  "not reached yet"
            if s:
                conn = None
                try:
                    conn, _ = chan.accept()
                    try:
                        conn.send("checkpoint %s is %s\0" % (chkpt_lbl, status))
                    except:
                        exc = sys.exc_info()[1]
                        if (isinstance(exc, OSError) or isinstance(exc, IOError)) and \
                           exc.errno == EPIPE:
                            logging.debug('checkpoint client disconnected')
                        else:
                            raise
                finally:
                    if conn:
                        conn.close()

    def start_checkpoint_thread(self):
        """prepare and start checkpoint service"""
        if self.checkpoint_thread or not (
          getattr(gconf, 'state_socket_unencoded', None) and getattr(gconf, 'socketdir', None)
        ):
            return
        chan = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        state_socket = os.path.join(gconf.socketdir, md5hex(gconf.state_socket_unencoded) + ".socket")
        try:
            os.unlink(state_socket)
        except:
            if sys.exc_info()[0] == OSError:
                pass
        chan.bind(state_socket)
        chan.listen(1)
        chkpt = gconf.configinterface.get_realtime("checkpoint")
        t = Thread(target=self.checkpt_service, args=(chan, chkpt))
        t.start()
        self.checkpoint_thread = t

    def add_job(self, path, label, job, *a, **kw):
        """insert @job function to job table at @path with @label"""
        if self.jobtab.get(path) == None:
            self.jobtab[path] = []
        self.jobtab[path].append((label, a, lambda : job(*a, **kw)))

    def add_failjob(self, path, label):
        """invoke .add_job with a job that does nothing just fails"""
        logging.debug('salvaged: ' + label)
        self.add_job(path, label, lambda: False)

    def wait(self, path, *args):
        """perform jobs registered for @path

        Reset jobtab entry for @path,
        determine success as the conjuction of
        success of all the jobs. In case of
        success, call .sendmark on @path
        """
        jobs = self.jobtab.pop(path, [])
        succeed = True
        for j in jobs:
            ret = j[-1]()
            if not ret:
                succeed = False
        if succeed and not args[0] == None:
            self.sendmark(path, *args)
        return succeed

    def sendmark(self, path, mark, adct=None):
        """update slave side xtime for @path to master side xtime

        also can send a setattr payload (see Server.setattr).
        """
        if adct:
            self.slave.server.setattr(path, adct)
        self.set_slave_xtime(path, mark)

class GMasterChangelogMixin(GMasterCommon):
    """ changelog based change detection and syncing """

    # index for change type and entry
    IDX_START = 0
    IDX_END   = 2

    POS_GFID   = 0
    POS_TYPE   = 1
    POS_ENTRY1 = -1

    TYPE_META  = "M "
    TYPE_GFID  = "D "
    TYPE_ENTRY = "E "

    # flat directory heirarchy for gfid based access
    FLAT_DIR_HIERARCHY = '.'

    # maximum retries per changelog before giving up
    MAX_RETRIES = 10

    def fallback_xsync(self):
        logging.info('falling back to xsync mode')
        gconf.configinterface.set('change-detector', 'xsync')
        selfkill()

    def setup_working_dir(self):
        workdir = os.path.join(gconf.working_dir, md5hex(gconf.local_path))
        logfile = os.path.join(workdir, 'changes.log')
        logging.debug('changelog working dir %s (log: %s)' % (workdir, logfile))
        return (workdir, logfile)

    def process_change(self, change, done, retry):
        pfx = gauxpfx()
        clist   = []
        entries = []
        meta_gfid = set()
        datas = set()

        # basic crawl stats: files and bytes
        files_pending  = {'count': 0, 'purge': 0, 'bytes': 0, 'files': []}
        try:
            f = open(change, "r")
            clist = f.readlines()
            f.close()
        except IOError:
            raise

        def edct(op, **ed):
            dct = {}
            dct['op'] = op
            for k in ed:
                if k == 'stat':
                    st = ed[k]
                    dst = dct['stat'] = {}
                    dst['uid'] = st.st_uid
                    dst['gid'] = st.st_gid
                    dst['mode'] = st.st_mode
                else:
                    dct[k] = ed[k]
            return dct

        # entry counts (not purges)
        def entry_update():
            files_pending['count'] += 1

        # purge count
        def purge_update():
            files_pending['purge'] += 1

        for e in clist:
            e = e.strip()
            et = e[self.IDX_START:self.IDX_END]   # entry type
            ec = e[self.IDX_END:].split(' ')      # rest of the bits

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
                    purge_update()
                    entries.append(edct(ty, gfid=gfid, entry=en))
                elif ty in ['CREATE', 'MKDIR', 'MKNOD']:
                    entry_update()
                    # stat information present in the changelog itself
                    entries.append(edct(ty, gfid=gfid, entry=en, mode=int(ec[2]),\
                                        uid=int(ec[3]), gid=int(ec[4])))
                else:
                    # stat() to get mode and other information
                    go = os.path.join(pfx, gfid)
                    st = lstat(go)
                    if isinstance(st, int):
                        if ty == 'RENAME': # special hack for renames...
                            entries.append(edct('UNLINK', gfid=gfid, entry=en))
                        else:
                            logging.debug('file %s got purged in the interim' % go)
                        continue

                    if ty == 'LINK':
                        entry_update()
                        entries.append(edct(ty, stat=st, entry=en, gfid=gfid))
                    elif ty == 'SYMLINK':
                        rl = errno_wrap(os.readlink, [en], [ENOENT])
                        if isinstance(rl, int):
                            continue
                        entry_update()
                        entries.append(edct(ty, stat=st, entry=en, gfid=gfid, link=rl))
                    elif ty == 'RENAME':
                        entry_update()
                        e1 = unescape(os.path.join(pfx, ec[self.POS_ENTRY1 - 1]))
                        entries.append(edct(ty, gfid=gfid, entry=e1, entry1=en, stat=st))
                    else:
                        logging.warn('ignoring %s [op %s]' % (gfid, ty))
            elif et == self.TYPE_GFID:
                datas.add(os.path.join(pfx, ec[0]))
            elif et == self.TYPE_META:
                if ec[1] == 'SETATTR': # only setattr's for now...
                    meta_gfid.add(os.path.join(pfx, ec[0]))
            else:
                logging.warn('got invalid changelog type: %s' % (et))
        logging.debug('entries: %s' % repr(entries))
        if not retry:
            self.update_worker_cumilitive_status(files_pending)
        # sync namespace
        if (entries):
            self.slave.server.entry_ops(entries)
        # sync metadata
        if (meta_gfid):
            meta_entries = []
            for go in meta_gfid:
                st = lstat(go)
                if isinstance(st, int):
                    logging.debug('file %s got purged in the interim' % go)
                    continue
                meta_entries.append(edct('META', go=go, stat=st))
            if meta_entries:
                self.slave.server.meta_ops(meta_entries)
        # sync data
        if datas:
            self.a_syncdata(datas)

    def process(self, changes, done=1):
        tries = 0
        retry = False

        while True:
            self.skipped_gfid_list = []
            self.current_files_skipped_count = 0

            # first, fire all changelog transfers in parallel. entry and metadata
            # are performed synchronously, therefore in serial. However at the end
            # of each changelog, data is synchronized with syncdata_async() - which
            # means it is serial w.r.t entries/metadata of that changelog but
            # happens in parallel with data of other changelogs.

            for change in changes:
                logging.debug('processing change %s' % change)
                self.process_change(change, done, retry)
                if not retry:
                    self.turns += 1 # number of changelogs processed in the batch

            # Now we wait for all the data transfers fired off in the above step
            # to complete. Note that this is not ideal either. Ideally we want to
            # trigger the entry/meta-data transfer of the next batch while waiting
            # for the data transfer of the current batch to finish.

            # Note that the reason to wait for the data transfer (vs doing it
            # completely in the background and call the changelog_done()
            # asynchronously) is because this waiting acts as a "backpressure"
            # and prevents a spiraling increase of wait stubs from consuming
            # unbounded memory and resources.

            # update the slave's time with the timestamp of the _last_ changelog
            # file time suffix. Since, the changelog prefix time is the time when
            # the changelog was rolled over, introduce a tolerence of 1 second to
            # counter the small delta b/w the marker update and gettimeofday().
            # NOTE: this is only for changelog mode, not xsync.

            # @change is the last changelog (therefore max time for this batch)
            if self.syncdata_wait():
                if done:
                    xtl = (int(change.split('.')[-1]) - 1, 0)
                    self.upd_stime(xtl)
                    map(self.master.server.changelog_done, changes)
                self.update_worker_files_syncd()
                break

            # We do not know which changelog transfer failed, retry everything.
            retry = True
            tries += 1
            if tries == self.MAX_RETRIES:
                logging.warn('changelogs %s could not be processed - moving on...' % \
                             ' '.join(map(os.path.basename, changes)))
                self.update_worker_total_files_skipped(self.current_files_skipped_count)
                logging.warn('SKIPPED GFID = %s' % ','.join(self.skipped_gfid_list))
                self.update_worker_files_syncd()
                if done:
                    xtl = (int(change.split('.')[-1]) - 1, 0)
                    self.upd_stime(xtl)
                    map(self.master.server.changelog_done, changes)
                break
            # it's either entry_ops() or Rsync that failed to do it's
            # job. Mostly it's entry_ops() [which currently has a problem
            # of failing to create an entry but failing to return an errno]
            # Therefore we do not know if it's either Rsync or the freaking
            # entry_ops() that failed... so we retry the _whole_ changelog
            # again.
            # TODO: remove entry retries when it's gets fixed.
            logging.warn('incomplete sync, retrying changelogs: %s' % \
                         ' '.join(map(os.path.basename, changes)))
            time.sleep(0.5)

    def upd_stime(self, stime, path=None):
        if not path:
            path = self.FLAT_DIR_HIERARCHY
        if not stime == URXTIME:
            self.sendmark(path, stime)

    def get_worker_status_file(self):
        file_name = gconf.local_path+'.status'
        file_name = file_name.replace("/", "_")
        worker_status_file = gconf.georep_session_working_dir+file_name
        return worker_status_file

    def update_worker_status(self, key, value):
        default_data = {"remote_node":"N/A",
                        "worker status":"Not Started",
                        "crawl status":"N/A",
                        "files_syncd": 0,
                        "files_remaining": 0,
                        "bytes_remaining": 0,
                        "purges_remaining": 0,
                        "total_files_skipped": 0}
        worker_status_file = self.get_worker_status_file()
        try:
            with open(worker_status_file, 'r+') as f:
                loaded_data = json.load(f)
                loaded_data[key] = value
                os.ftruncate(f.fileno(), 0)
                os.lseek(f.fileno(), 0, os.SEEK_SET)
                json.dump(loaded_data, f)
                f.flush()
                os.fsync(f.fileno())
        except (IOError, OSError, ValueError):
            logging.info ('Creating new %s' % worker_status_file)
            try:
                with open(worker_status_file, 'wb') as f:
                    default_data[key] = value
                    json.dump(default_data, f)
                    f.flush()
                    os.fsync(f.fileno())
            except:
                raise

    def update_worker_cumilitive_status(self, files_pending):
        default_data = {"remote_node":"N/A",
                        "worker status":"Not Started",
                        "crawl status":"N/A",
                        "files_syncd": 0,
                        "files_remaining": 0,
                        "bytes_remaining": 0,
                        "purges_remaining": 0,
                        "total_files_skipped": 0}
        worker_status_file = self.get_worker_status_file()
        try:
            with open(worker_status_file, 'r+') as f:
                loaded_data = json.load(f)
                loaded_data['files_remaining']  = files_pending['count']
                loaded_data['bytes_remaining']  = files_pending['bytes']
                loaded_data['purges_remaining'] = files_pending['purge']
                os.ftruncate(f.fileno(), 0)
                os.lseek(f.fileno(), 0, os.SEEK_SET)
                json.dump(loaded_data, f)
                f.flush()
                os.fsync(f.fileno())
        except (IOError, OSError, ValueError):
            logging.info ('Creating new %s' % worker_status_file)
            try:
                with open(worker_status_file, 'wb') as f:
                    default_data['files_remaining']  = files_pending['count']
                    default_data['bytes_remaining']  = files_pending['bytes']
                    default_data['purges_remaining'] = files_pending['purge']
                    json.dump(default_data, f)
                    f.flush()
                    os.fsync(f.fileno())
            except:
                raise

    def update_worker_remote_node (self):
        node = sys.argv[-1]
        node = node.split("@")[-1]
        remote_node_ip = node.split(":")[0]
        remote_node_vol = node.split(":")[3]
        remote_node = remote_node_ip + '::' + remote_node_vol
        self.update_worker_status ('remote_node', remote_node)

    def update_worker_health (self, state):
        self.update_worker_status ('worker status', state)

    def update_worker_crawl_status (self, state):
        self.update_worker_status ('crawl status', state)

    def update_worker_files_syncd (self):
        default_data = {"remote_node":"N/A",
                        "worker status":"Not Started",
                        "crawl status":"N/A",
                        "files_syncd": 0,
                        "files_remaining": 0,
                        "bytes_remaining": 0,
                        "purges_remaining": 0,
                        "total_files_skipped": 0}
        worker_status_file = self.get_worker_status_file()
        try:
            with open(worker_status_file, 'r+') as f:
                loaded_data = json.load(f)
                loaded_data['files_syncd'] += loaded_data['files_remaining']
                loaded_data['files_remaining']  = 0
                loaded_data['bytes_remaining']  = 0
                loaded_data['purges_remaining'] = 0
                os.ftruncate(f.fileno(), 0)
                os.lseek(f.fileno(), 0, os.SEEK_SET)
                json.dump(loaded_data, f)
                f.flush()
                os.fsync(f.fileno())
        except (IOError, OSError, ValueError):
            logging.info ('Creating new %s' % worker_status_file)
            try:
                with open(worker_status_file, 'wb') as f:
                    json.dump(default_data, f)
                    f.flush()
                    os.fsync(f.fileno())
            except:
                raise

    def update_worker_files_remaining (self, state):
        self.update_worker_status ('files_remaining', state)

    def update_worker_bytes_remaining (self, state):
        self.update_worker_status ('bytes_remaining', state)

    def update_worker_purges_remaining (self, state):
        self.update_worker_status ('purges_remaining', state)

    def update_worker_total_files_skipped (self, value):
        default_data = {"remote_node":"N/A",
                        "worker status":"Not Started",
                        "crawl status":"N/A",
                        "files_syncd": 0,
                        "files_remaining": 0,
                        "bytes_remaining": 0,
                        "purges_remaining": 0,
                        "total_files_skipped": 0}
        worker_status_file = self.get_worker_status_file()
        try:
            with open(worker_status_file, 'r+') as f:
                loaded_data = json.load(f)
                loaded_data['total_files_skipped'] = value
                loaded_data['files_remaining'] -= value
                os.ftruncate(f.fileno(), 0)
                os.lseek(f.fileno(), 0, os.SEEK_SET)
                json.dump(loaded_data, f)
                f.flush()
                os.fsync(f.fileno())
        except (IOError, OSError, ValueError):
            logging.info ('Creating new %s' % worker_status_file)
            try:
                with open(worker_status_file, 'wb') as f:
                    default_data['total_files_skipped'] = value
                    json.dump(default_data, f)
                    f.flush()
                    os.fsync(f.fileno())
            except:
                raise

    def crawl(self):
        self.update_worker_crawl_status("Changelog Crawl")
        changes = []
        # get stime (from the brick) and purge changelogs
        # that are _historical_ to that time.
        purge_time = self.xtime('.', self.slave)
        if isinstance(purge_time, int):
            purge_time = None
        try:
            self.master.server.changelog_scan()
            self.crawls += 1
        except OSError:
            self.fallback_xsync()
            self.update_worker_crawl_status("Hybrid Crawl")
        changes = self.master.server.changelog_getchanges()
        if changes:
            if purge_time:
                logging.info("slave's time: %s" % repr(purge_time))
                processed = [x for x in changes if int(x.split('.')[-1]) < purge_time[0]]
                for pr in processed:
                    logging.info('skipping already processed change: %s...' % os.path.basename(pr))
                    self.master.server.changelog_done(pr)
                    changes.remove(pr)
            logging.debug('processing changes %s' % repr(changes))
            self.process(changes)

    def register(self):
        (workdir, logfile) = self.setup_working_dir()
        self.sleep_interval = int(gconf.change_interval)
        # register with the changelog library
        try:
            # 9 == log level (DEBUG)
            # 5 == connection retries
            self.master.server.changelog_register(gconf.local_path,
                                                  workdir, logfile, 9, 5)
        except OSError:
            self.fallback_xsync()
            # control should not reach here
            raise

class GMasterXsyncMixin(GMasterChangelogMixin):
    """
    This crawl needs to be xtime based (as of now
    it's not. this is beacuse we generate CHANGELOG
    file during each crawl which is then processed
    by process_change()).
    For now it's used as a one-shot initial sync
    mechanism and only syncs directories, regular
    files, hardlinks and symlinks.
    """

    XSYNC_MAX_ENTRIES = 1<<13

    def register(self):
        self.counter = 0
        self.comlist = []
        self.stimes = []
        self.sleep_interval = 60
        self.tempdir = self.setup_working_dir()[0]
        self.tempdir = os.path.join(self.tempdir, 'xsync')
        logging.info('xsync temp directory: %s' % self.tempdir)
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
        additionally terminates itself on recieving a 'finale' event
        """
        def Xsyncer():
            self.Xcrawl()
        t = Thread(target=Xsyncer)
        t.start()
        logging.info('starting hybrid crawl...')
        self.update_worker_crawl_status("Hybrid Crawl")
        while True:
            try:
                item = self.comlist.pop(0)
                if item[0] == 'finale':
                    logging.info('finished hybrid crawl syncing')
                    break
                elif item[0] == 'xsync':
                    logging.info('processing xsync changelog %s' % (item[1]))
                    self.process([item[1]], 0)
                elif item[0] == 'stime':
                    logging.debug('setting slave time: %s' % repr(item[1]))
                    self.upd_stime(item[1][1], item[1][0])
                else:
                    logging.warn('unknown tuple in comlist (%s)' % repr(item))
            except IndexError:
                time.sleep(1)

    def write_entry_change(self, prefix, data=[]):
        self.fh.write("%s %s\n" % (prefix, ' '.join(data)))

    def open(self):
        try:
            self.xsync_change = os.path.join(self.tempdir, 'XSYNC-CHANGELOG.' + str(int(time.time())))
            self.fh = open(self.xsync_change, 'w')
        except IOError:
            raise

    def close(self):
        self.fh.close()

    def fname(self):
        return self.xsync_change

    def put(self, mark, item):
        self.comlist.append((mark, item))

    def sync_xsync(self, last):
        """schedule a processing of changelog"""
        self.close()
        self.put('xsync', self.fname())
        self.counter = 0
        if not last:
            time.sleep(1) # make sure changelogs are 1 second apart
            self.open()

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

    def Xcrawl(self, path='.', xtr_root=None):
        """
        generate a CHANGELOG file consumable by process_change.

        slave's xtime (stime) is _cached_ for comparisons across
        the filesystem tree, but set after directory synchronization.
        """
        if path == '.':
            self.open()
            self.crawls += 1
        if not xtr_root:
            # get the root stime and use it for all comparisons
            xtr_root = self.xtime('.', self.slave)
            if isinstance(xtr_root, int):
                if xtr_root != ENOENT:
                    logging.warn("slave cluster not returning the " \
                                 "correct xtime for root (%d)" % xtr_root)
                xtr_root = self.minus_infinity
        xtl = self.xtime(path)
        if isinstance(xtl, int):
            logging.warn("master cluster's xtime not found")
        xtr = self.xtime(path, self.slave)
        if isinstance(xtr, int):
            if xtr != ENOENT:
                logging.warn("slave cluster not returning the " \
                             "correct xtime for %s (%d)" % (path, xtr))
            xtr = self.minus_infinity
        xtr = max(xtr, xtr_root)
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
                logging.warn("irregular xtime for %s: %s" % (e, errno.errorcode[xte]))
                continue
            if not self.need_sync(e, xte, xtr):
                continue
            st = self.master.server.lstat(e)
            if isinstance(st, int):
                logging.warn('%s got purged in the interim ...' % e)
                continue
            gfid = self.master.server.gfid(e)
            if isinstance(gfid, int):
                logging.warn('skipping entry %s..' % e)
                continue
            mo = st.st_mode
            self.counter += 1
            if self.counter == self.XSYNC_MAX_ENTRIES:
                self.sync_done(self.stimes, False)
                self.stimes = []
            if stat.S_ISDIR(mo):
                self.write_entry_change("E", [gfid, 'MKDIR', str(mo), str(st.st_uid), str(st.st_gid), escape(os.path.join(pargfid, bname))])
                self.Xcrawl(e, xtr_root)
                self.stimes.append((e, xte))
            elif stat.S_ISLNK(mo):
                self.write_entry_change("E", [gfid, 'SYMLINK', escape(os.path.join(pargfid, bname))])
            elif stat.S_ISREG(mo):
                nlink = st.st_nlink
                nlink -= 1 # fixup backend stat link count
                # if a file has a hardlink, create a Changelog entry as 'LINK' so the slave
                # side will decide if to create the new entry, or to create link.
                if nlink == 1:
                    self.write_entry_change("E", [gfid, 'MKNOD', str(mo), str(st.st_uid), str(st.st_gid), escape(os.path.join(pargfid, bname))])
                else:
                    self.write_entry_change("E", [gfid, 'LINK', escape(os.path.join(pargfid, bname))])
                self.write_entry_change("D", [gfid])
        if path == '.':
            self.stimes.append((path, xtl))
            self.sync_done(self.stimes, True)

class BoxClosedErr(Exception):
    pass

class PostBox(list):
    """synchronized collection for storing things thought of as "requests" """

    def __init__(self, *a):
        list.__init__(self, *a)
        # too bad Python stdlib does not have read/write locks...
        # it would suffivce to grab the lock in .append as reader, in .close as writer
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
            po = self.sync_engine(pb)
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
