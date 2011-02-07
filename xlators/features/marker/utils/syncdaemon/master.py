import os
import sys
import time
import stat
import signal
import logging
import errno
from errno import ENOENT, ENODATA
from threading import Thread, currentThread, Condition, Lock

from gconf import gconf

URXTIME = (-1, 0)

class GMaster(object):

    def get_volinfo(self):
        self.volume_info = self.master.server.volume_info()
        if self.volume_info['retval']:
            raise RuntimeError("master is corrupt")
        return self.volume_info

    @property
    def uuid(self):
        if not getattr(self, '_uuid', None):
            self._uuid = self.volume_info['uuid']
        return self._uuid

    @property
    def volmark(self):
        return self.volume_info['volume_mark']

    def xtime(self, path, *a, **opts):
        if a:
            rsc = a[0]
        else:
            rsc = self.master
        if not 'create' in opts:
            opts['create'] = rsc == self.master
        xt = rsc.server.xtime(path, self.uuid)
        if (isinstance(xt, int) or xt < self.volmark) and opts['create']:
            t = time.time()
            sec = int(t)
            nsec = int((t - sec) * 1000000)
            xt = (sec, nsec)
            rsc.server.set_xtime(path, self.uuid, xt)
        if xt == ENODATA:
            xt = URXTIME
        return xt

    def __init__(self, master, slave):
        self.master = master
        self.slave = slave
        self.get_volinfo()
        self.jobtab = {}
        self.syncer = Syncer(slave)
        self.total_turns = int(gconf.turns)
        self.turns = 0
        self.start = None
        self.change_seen = None
        logging.info('master started on ' + self.uuid)
        while True:
            self.crawl()

    def add_job(self, path, label, job, *a, **kw):
        if self.jobtab.get(path) == None:
            self.jobtab[path] = []
        self.jobtab[path].append((label, a, lambda : job(*a, **kw)))

    def wait(self, path, *args):
        jobs = self.jobtab.pop(path, [])
        succeed = True
        for j in jobs:
            ret = j[-1]()
            if not ret:
                succeed = False
        if succeed:
            self.sendmark(path, *args)
        return succeed

    def sendmark(self, path, mark, adct=None):
        if adct:
            self.slave.server.setattr(path, adct)
        self.slave.server.set_xtime(path, self.uuid, mark)

    def crawl(self, path='.', xtl=None):
        if path == '.':
            if self.start:
                logging.info("crawl took %.6f" % (time.time() - self.start))
            time.sleep(1)
            self.start = time.time()
            logging.info("crawling...")
            self.get_volinfo()
            if self.volume_info['uuid'] != self.uuid:
                raise RuntimeError("master uuid mismatch")
        logging.debug("entering " + path)
        if not xtl:
            xtl = self.xtime(path)
        xtr0 = self.xtime(path, self.slave)
        if isinstance(xtr0, int):
            xtr = URXTIME
        else:
            xtr = xtr0
        if xtr0 == ENOENT:
            self.slave.server.mkdir(path)
        else:
            if xtr > xtl:
                raise RuntimeError("timestamp corruption for " + path)
            if xtl == xtr:
                if path == '.' and self.total_turns and self.change_seen:
                    self.turns += 1
                    self.change_seen = False
                    logging.info("finished turn #%s/%s" % (self.turns, self.total_turns))
                    if self.turns == self.total_turns:
                        logging.info("reached turn limit, terminating.")
                        os.kill(os.getpid(), signal.SIGTERM)
                return
        if path == '.':
            self.change_seen = True
        dem, des = ( x.server.entries(path) for x in (self.master, self.slave) )
        dd = set(des) - set(dem)
        if dd:
            self.slave.server.purge(path, dd)
        chld = []
        for e in dem:
            e = os.path.join(path, e)
            xte = self.xtime(e)
            if isinstance(xte, int):
                logging.warn("irregular xtime for %s: %s" % (e, errno.errorcode[xte]))
            elif xte > xtr:
                chld.append((e, xte))
        def indulgently(e, fnc, blame=None):
            if not blame:
                blame = path
            try:
                return fnc(e)
            except (IOError, OSError):
                ex = sys.exc_info()[1]
                if ex.errno == ENOENT:
                    logging.warn("salvaged ENOENT for" + e)
                    self.add_job(blame, 'salvage', lambda: False)
                    return False
                else:
                    raise
        for e, xte in chld:
            st = indulgently(e, lambda e: os.lstat(e))
            if st == False:
                continue
            mo = st.st_mode
            adct = {'own': (st.st_uid, st.st_gid)}
            if stat.S_ISLNK(mo):
                if indulgently(e, lambda e: self.slave.server.symlink(os.readlink(e), e)) == False:
                    continue
                self.sendmark(e, xte, adct)
            elif stat.S_ISREG(mo):
                logging.debug("syncing %s ..." % e)
                pb = self.syncer.add(e)
                def regjob(e, xte, pb):
                    if pb.wait():
                        logging.debug("synced " + e)
                        self.sendmark(e, xte)
                        return True
                    else:
                        logging.error("failed to sync " + e)
                self.add_job(path, 'reg', regjob, e, xte, pb)
            elif stat.S_ISDIR(mo):
                adct['mode'] = mo
                if indulgently(e, lambda e: (self.add_job(path, 'cwait', self.wait, e, xte, adct),
                                             self.crawl(e, xte),
                                             True)[-1], blame=e) == False:
                    continue
            else:
                # ignore fifos, sockets and special files
                pass
        if path == '.':
            self.wait(path, xtl)

class BoxClosedErr(Exception):
    pass

class PostBox(list):

    def __init__(self, *a):
        list.__init__(self, *a)
        self.lever = Condition()
        self.open = True
        self.done = False

    def wait(self):
        self.lever.acquire()
        if not self.done:
            self.lever.wait()
        self.lever.release()
        return self.result

    def wakeup(self, data):
        self.result = data
        self.lever.acquire()
        self.done = True
        self.lever.notifyAll()
        self.lever.release()

    def append(self, e):
        self.lever.acquire()
        if not self.open:
            raise BoxClosedErr
        list.append(self, e)
        self.lever.release()

    def close(self):
        self.lever.acquire()
        self.open = False
        self.lever.release()

class Syncer(object):

    def __init__(self, slave):
        self.slave = slave
        self.lock = Lock()
        self.pb = PostBox()
        for i in range(int(gconf.sync_jobs)):
            t = Thread(target=self.syncjob)
            t.setDaemon = True
            t.start()

    def syncjob(self):
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
            pb.wakeup(self.slave.rsync(pb))

    def add(self, e):
        while True:
            try:
                self.pb.append(e)
                return self.pb
            except BoxClosedErr:
                pass
