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
from errno import ENOENT, ENODATA, EPIPE, EEXIST
from threading import currentThread, Condition, Lock
from datetime import datetime

from gconf import gconf
from tempfile import mkdtemp, NamedTemporaryFile
from syncdutils import FreeObject, Thread, GsyncdError, boolify, escape, \
                       unescape, select, gauxpfx, md5hex, selfkill, entry2pb, \
                       lstat

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
    class _GMaster(crawlmixin, modemixin, sendmarkmixin, purgemixin):
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

    def xtime_low(self, server, path, **opts):
        xt = server.xtime(path, self.uuid)
        if isinstance(xt, int) and xt != ENODATA:
            return xt
        if xt == ENODATA or xt < self.volmark:
            if opts['create']:
                xt = _xtime_now()
                server.aggregated.set_xtime(path, self.uuid, xt)
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
        self.slave.server.set_xtime(path, self.uuid, mark)
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
        return self.xtime_low(rsc.server, path, **opts)

    def get_initial_crawl_data(self):
        # while persisting only 'files_syncd' is non-zero, rest of
        # the stats are nulls. lets keep it that way in case they
        # are needed to be used some day...
        default_data = {'files_syncd': 0,
                        'files_remaining': 0,
                        'bytes_remaining': 0,
                        'purges_remaining': 0}
        if getattr(gconf, 'state_detail_file', None):
            try:
                return json.load(open(gconf.state_detail_file))
            except (IOError, OSError):
                ex = sys.exc_info()[1]
                if ex.errno == ENOENT:
                    # Create file with initial data
                    with open(gconf.state_detail_file, 'wb') as f:
                        json.dump(default_data, f)
                    return default_data
                else:
                    raise
        return default_data

    def update_crawl_data(self):
        if getattr(gconf, 'state_detail_file', None):
            try:
                same_dir = os.path.dirname(gconf.state_detail_file)
                with NamedTemporaryFile(dir=same_dir, delete=False) as tmp:
                    json.dump(self.total_crawl_stats, tmp)
                    os.rename(tmp.name, gconf.state_detail_file)
            except (IOError, OSError):
                raise

    def __init__(self, master, slave):
        self.master = master
        self.slave = slave
        self.jobtab = {}
        self.syncer = Syncer(slave)
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
                raise GsyncdError("master is corrupt")
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
            if not crawl:
                time.sleep(5)
                continue
            self.crawl()
            if oneshot:
                return
            time.sleep(self.sleep_interval)

    @classmethod
    def _checkpt_param(cls, chkpt, prm, xtimish=True):
        """use config backend to lookup a parameter belonging to
           checkpoint @chkpt"""
        cprm = getattr(gconf, 'checkpoint_' + prm, None)
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

    def get_extra_info(self):
        str_info = '\nUptime=%s;FilesSyncd=%d;FilesPending=%d;BytesPending=%d;DeletesPending=%d;' % \
            (self._crawl_time_format(datetime.now() - self.crawl_start), \
                 self.total_crawl_stats['files_syncd'], \
                 self.total_crawl_stats['files_remaining'], \
                 self.total_crawl_stats['bytes_remaining'], \
                 self.total_crawl_stats['purges_remaining'])
        str_info += '\0'
        logging.debug(str_info)
        return str_info

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

    def checkpt_service(self, chan, chkpt, tgt):
        """checkpoint service loop

        monitor and verify checkpoint status for @chkpt, and listen
        for incoming requests for whom we serve a pretty-formatted
        status report"""
        if not chkpt:
            # dummy loop for the case when there is no checkpt set
            while True:
                select([chan], [], [])
                conn, _ = chan.accept()
                conn.send(self.get_extra_info())
                conn.close()
        completed = self._checkpt_param(chkpt, 'completed', xtimish=False)
        if completed:
            completed = tuple(int(x) for x in completed.split('.'))
        while True:
            s,_,_ = select([chan], [], [], (not completed) and 5 or None)
            # either request made and we re-check to not
            # give back stale data, or we still hunting for completion
            if self.native_xtime(tgt) and self.native_xtime(tgt) < self.volmark:
                # indexing has been reset since setting the checkpoint
                status = "is invalid"
            else:
                xtr = self.xtime('.', self.slave)
                if isinstance(xtr, int):
                    raise GsyncdError("slave root directory is unaccessible (%s)",
                                      os.strerror(xtr))
                ncompleted = self.xtime_geq(xtr, tgt)
                if completed and not ncompleted: # stale data
                    logging.warn("completion time %s for checkpoint %s became stale" % \
                                 (self.humantime(*completed), chkpt))
                    completed = None
                    gconf.confdata.delete('checkpoint-completed')
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
                        conn.send("  | checkpoint %s %s %s" % (chkpt, status, self.get_extra_info()))
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
        checkpt_tgt = None
        if gconf.checkpoint:
            checkpt_tgt = self._checkpt_param(gconf.checkpoint, 'target')
            if not checkpt_tgt:
                checkpt_tgt = self.xtime('.')
                if isinstance(checkpt_tgt, int):
                    raise GsyncdError("master root directory is unaccessible (%s)",
                                      os.strerror(checkpt_tgt))
                self._set_checkpt_param(gconf.checkpoint, 'target', checkpt_tgt)
            logging.debug("checkpoint target %s has been determined for checkpoint %s" % \
                          (repr(checkpt_tgt), gconf.checkpoint))
        t = Thread(target=self.checkpt_service, args=(chan, gconf.checkpoint, checkpt_tgt))
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
    POS_ENTRY1 = 2
    POS_ENTRY2 = 3  # renames

    _CL_TYPE_DATA_PFX     = "D "
    _CL_TYPE_METADATA_PFX = "M "
    _CL_TYPE_ENTRY_PFX    = "E "

    TYPE_GFID  = [_CL_TYPE_DATA_PFX] # ignoring metadata ops
    TYPE_ENTRY = [_CL_TYPE_ENTRY_PFX]

    # flat directory heirarchy for gfid based access
    FLAT_DIR_HIERARCHY = '.'

    def fallback_xsync(self):
        logging.info('falling back to xsync mode')
        gconf.configinterface.set('change-detector', 'xsync')
        selfkill()

    def setup_working_dir(self):
        workdir = os.path.join(gconf.working_dir, md5hex(gconf.local_path))
        logfile = os.path.join(workdir, 'changes.log')
        logging.debug('changelog working dir %s (log: %s)' % (workdir, logfile))
        return (workdir, logfile)

    # update stats from *this* crawl
    def update_cumulative_stats(self, files_pending):
        self.total_crawl_stats['files_remaining']  = files_pending['count']
        self.total_crawl_stats['bytes_remaining']  = files_pending['bytes']
        self.total_crawl_stats['purges_remaining'] = files_pending['purge']

    # sync data
    def syncdata(self, datas):
        logging.debug('datas: %s' % (datas))
        for data in datas:
            logging.debug('candidate for syncing %s' % data)
            pb = self.syncer.add(data)
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
            self.add_job(self.FLAT_DIR_HIERARCHY, 'reg', regjob, data, None, pb)
        if self.wait(self.FLAT_DIR_HIERARCHY, None):
            return True

    def process_change(self, change, done, retry):
        pfx = gauxpfx()
        clist   = []
        entries = []
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

        # regular file update: bytes & count
        def _update_reg(entry, size):
            if not entry in files_pending['files']:
                files_pending['count'] += 1
                files_pending['bytes'] += size
                files_pending['files'].append(entry)
        # updates for directories, symlinks etc..
        def _update_rest():
            files_pending['count'] += 1

        # entry count
        def entry_update(entry, size, mode):
            if stat.S_ISREG(mode):
                _update_reg(entry, size)
            else:
                _update_rest()
        # purge count
        def purge_update():
            files_pending['purge'] += 1

        for e in clist:
            e = e.strip()
            et = e[self.IDX_START:self.IDX_END]
            ec = e[self.IDX_END:].split(' ')
            if et in self.TYPE_ENTRY:
                ty = ec[self.POS_TYPE]
                en = unescape(os.path.join(pfx, ec[self.POS_ENTRY1]))
                gfid = ec[self.POS_GFID]
                # definitely need a better way bucketize entry ops
                if ty in ['UNLINK', 'RMDIR']:
                    purge_update()
                    entries.append(edct(ty, gfid=gfid, entry=en))
                    continue
                go = os.path.join(pfx, gfid)
                st = lstat(go)
                if isinstance(st, int):
                    logging.debug('file %s got purged in the interim' % go)
                    continue
                entry_update(go, st.st_size, st.st_mode)
                if ty in ['CREATE', 'MKDIR', 'MKNOD']:
                    entries.append(edct(ty, stat=st, entry=en, gfid=gfid))
                elif ty == 'LINK':
                    entries.append(edct(ty, stat=st, entry=en, gfid=gfid))
                elif ty == 'SYMLINK':
                    entries.append(edct(ty, stat=st, entry=en, gfid=gfid, link=os.readlink(en)))
                elif ty == 'RENAME':
                    e2 = unescape(os.path.join(pfx, ec[self.POS_ENTRY2]))
                    entries.append(edct(ty, gfid=gfid, entry=en, entry1=e2, stat=st))
                else:
                    logging.warn('ignoring %s [op %s]' % (gfid, ty))
            elif et in self.TYPE_GFID:
                go = os.path.join(pfx, ec[0])
                st = lstat(go)
                if isinstance(st, int):
                    logging.debug('file %s got purged in the interim' % go)
                    continue
                entry_update(go, st.st_size, st.st_mode)
                datas.update([go])
        logging.debug('entries: %s' % repr(entries))
        if not retry:
            self.update_cumulative_stats(files_pending)
        # sync namespace
        if (entries):
            self.slave.server.entry_ops(entries)
        # sync data
        if self.syncdata(datas):
            if done:
                self.master.server.changelog_done(change)
            return True

    def sync_done(self):
        self.total_crawl_stats['files_syncd'] += self.total_crawl_stats['files_remaining']
        self.total_crawl_stats['files_remaining']  = 0
        self.total_crawl_stats['bytes_remaining']  = 0
        self.total_crawl_stats['purges_remaining'] = 0
        self.update_crawl_data()

    def process(self, changes, done=1):
        for change in changes:
            retry = False
            while True:
                logging.debug('processing change %s' % change)
                if self.process_change(change, done, retry):
                    self.sync_done()
                    break
                retry = True
                # it's either entry_ops() or Rsync that failed to do it's
                # job. Mostly it's entry_ops() [which currently has a problem
                # of failing to create an entry but failing to return an errno]
                # Therefore we do not know if it's either Rsync or the freaking
                # entry_ops() that failed... so we retry the _whole_ changelog
                # again.
                # TODO: remove entry retries when it's gets fixed.
                logging.warn('incomplete sync, retrying changelog: %s' % change)
                time.sleep(0.5)
            self.turns += 1

    def upd_stime(self, stime):
        if not stime == URXTIME:
            self.sendmark(self.FLAT_DIR_HIERARCHY, stime)

    def crawl(self):
        changes = []
        try:
            self.master.server.changelog_scan()
            self.crawls += 1
        except OSError:
            self.fallback_xsync()
        changes = self.master.server.changelog_getchanges()
        if changes:
            xtl = self.xtime(self.FLAT_DIR_HIERARCHY)
            if isinstance(xtl, int):
                raise GsyncdError('master is corrupt')
            logging.debug('processing changes %s' % repr(changes))
            self.process(changes)
            self.upd_stime(xtl)

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
    files and symlinks.
    """

    def register(self):
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

    def crawl(self, path='.', xtr=None, done=0):
        """ generate a CHANGELOG file consumable by process_change """
        if path == '.':
            self.open()
            self.crawls += 1
        if not xtr:
            # get the root stime and use it for all comparisons
            xtr = self.xtime('.', self.slave)
            if isinstance(xtr, int):
                if xtr != ENOENT:
                    raise GsyncdError('slave is corrupt')
                xtr = self.minus_infinity
        xtl = self.xtime(path)
        if isinstance(xtl, int):
            raise GsyncdError('master is corrupt')
        if xtr == xtl:
            if path == '.':
                self.close()
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
            st = lstat(e)
            if isinstance(st, int):
                logging.warn('%s got purged in the interim..' % e)
                continue
            gfid = self.master.server.gfid(e)
            if isinstance(gfid, int):
                logging.warn('skipping entry %s..' % (e))
                continue
            xte = self.xtime(e)
            if isinstance(xte, int):
                raise GsyncdError('master is corrupt')
            if not self.need_sync(e, xte, xtr):
                continue
            mo = st.st_mode
            if stat.S_ISDIR(mo):
                self.write_entry_change("E", [gfid, 'MKDIR', escape(os.path.join(pargfid, bname))])
                self.crawl(e, xtr)
            elif stat.S_ISREG(mo):
                self.write_entry_change("E", [gfid, 'CREATE', escape(os.path.join(pargfid, bname))])
                self.write_entry_change("D", [gfid])
            elif stat.S_ISLNK(mo):
                self.write_entry_change("E", [gfid, 'SYMLINK', escape(os.path.join(pargfid, bname))])
            else:
                logging.info('ignoring %s' % e)
        if path == '.':
            logging.info('processing xsync changelog %s' % self.fname())
            self.close()
            self.process([self.fname()], done)
            self.upd_stime(xtl)

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

    def __init__(self, slave):
        """spawn worker threads"""
        self.slave = slave
        self.lock = Lock()
        self.pb = PostBox()
        self.bytes_synced = 0
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
            po = self.slave.rsync(pb)
            if po.returncode == 0:
                ret = (True, 0)
            elif po.returncode in (23, 24):
                # partial transfer (cf. rsync(1)), that's normal
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
