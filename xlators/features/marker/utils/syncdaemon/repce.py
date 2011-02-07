import os
import sys
import select
import time
import logging
from threading import Thread, Condition
try:
    import thread
except ImportError:
    # py 3
    import _thread as thread
try:
    from Queue import Queue
except ImportError:
    # py 3
    from queue import Queue
try:
    import cPickle as pickle
except ImportError:
    # py 3
    import pickle

pickle_proto = -1
repce_version = 1.0

def ioparse(i, o):
    if isinstance(i, int):
        i = os.fdopen(i)
    # rely on duck typing for recognizing
    # streams as that works uniformly
    # in py2 and py3
    if hasattr(o, 'fileno'):
        o = o.fileno()
    return (i, o)

def send(out, *args):
    os.write(out, pickle.dumps(args, pickle_proto))

def recv(inf):
    return pickle.load(inf)


class RepceServer(object):

    def __init__(self, obj, i, o, wnum=6):
        self.obj = obj
        self.inf, self.out = ioparse(i, o)
        self.wnum = wnum
        self.q = Queue()

    def service_loop(self):
        for i in range(self.wnum):
            t = Thread(target=self.worker)
            t.setDaemon(True)
            t.start()
        try:
            while True:
                self.q.put(recv(self.inf))
        except EOFError:
                logging.info("terminating on reaching EOF.")

    def worker(self):
        while True:
            in_data = self.q.get(True)
            rid = in_data[0]
            rmeth = in_data[1]
            exc = False
            try:
                res = getattr(self.obj, rmeth)(*in_data[2:])
            except:
                res = sys.exc_info()[1]
                exc = True
                logging.exception("call failed: ")
            send(self.out, rid, exc, res)


class RepceJob(object):

    def __init__(self, cbk):
        self.rid = (os.getpid(), thread.get_ident(), time.time())
        self.cbk = cbk
        self.lever = Condition()
        self.done = False

    def __repr__(self):
        return ':'.join([str(x) for x in self.rid])

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
        self.lever.notify()
        self.lever.release()


class RepceClient(object):

    def __init__(self, i, o):
        self.inf, self.out = ioparse(i, o)
        self.jtab = {}
        t = Thread(target = self.listen)
        t.setDaemon(True)
        t.start()

    def listen(self):
        while True:
            select.select((self.inf,), (), ())
            rid, exc, res = recv(self.inf)
            rjob = self.jtab.pop(rid)
            if rjob.cbk:
                rjob.cbk(rjob, [exc, res])

    def push(self, meth, *args, **kw):
        cbk = kw.get('cbk')
        if not cbk:
            def cbk(rj, res):
                if res[0]:
                    raise res[1]
        rjob = RepceJob(cbk)
        self.jtab[rjob.rid] = rjob
        logging.debug("call %s %s%s ..." % (repr(rjob), meth, repr(args)))
        send(self.out, rjob.rid, meth, *args)
        return rjob

    def __call__(self, meth, *args):
        rjob = self.push(meth, *args, **{'cbk': lambda rj, res: rj.wakeup(res)})
        exc, res = rjob.wait()
        if exc:
            logging.error('call %s (%s) failed on peer with %s' % (repr(rjob), meth, str(type(res).__name__)))
            raise res
        logging.debug("call %s %s -> %s" % (repr(rjob), meth, repr(res)))
        return res

    class mprx(object):

        def __init__(self, ins, meth):
            self.ins = ins
            self.meth = meth

        def __call__(self, *a):
            return self.ins(self.meth, *a)

    def __getattr__(self, meth):
        return self.mprx(self, meth)

    def __version__(self):
        d = {'proto': repce_version}
        try:
            d['object'] = self('version')
        except AttributeError:
            pass
        return d
