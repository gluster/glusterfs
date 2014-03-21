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
import logging
from threading import Condition
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

from syncdutils import Thread, select

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
    """pickle args and write out wholly in one syscall

    ie. not use the ability of pickle to dump directly to
    a stream, as that would potentially mess up messages
    by interleaving them
    """
    os.write(out, pickle.dumps(args, pickle_proto))


def recv(inf):
    """load an object from input stream"""
    return pickle.load(inf)


class RepceServer(object):

    """RePCe is Hungarian for canola, http://hu.wikipedia.org/wiki/Repce

    ... also our homebrewed RPC backend where the transport layer is
    reduced to a pair of filehandles.

    This is the server component.
    """

    def __init__(self, obj, i, o, wnum=6):
        """register a backend object .obj to which incoming messages
           are dispatched, also incoming/outcoming streams
        """
        self.obj = obj
        self.inf, self.out = ioparse(i, o)
        self.wnum = wnum
        self.q = Queue()

    def service_loop(self):
        """fire up worker threads, get messages and dispatch among them"""
        for i in range(self.wnum):
            t = Thread(target=self.worker)
            t.start()
        try:
            while True:
                self.q.put(recv(self.inf))
        except EOFError:
            logging.info("terminating on reaching EOF.")

    def worker(self):
        """life of a worker

        Get message, extract its id, method name and arguments
        (kwargs not supported), call method on .obj.
        Send back message id + return value.
        If method call throws an exception, rescue it, and send
        back the exception as result (with flag marking it as
        exception).
        """
        while True:
            in_data = self.q.get(True)
            rid = in_data[0]
            rmeth = in_data[1]
            exc = False
            if rmeth == '__repce_version__':
                res = repce_version
            else:
                try:
                    res = getattr(self.obj, rmeth)(*in_data[2:])
                except:
                    res = sys.exc_info()[1]
                    exc = True
                    logging.exception("call failed: ")
            send(self.out, rid, exc, res)


class RepceJob(object):

    """class representing message status we can use
    for waiting on reply"""

    def __init__(self, cbk):
        """
        - .rid: (process-wise) unique id
        - .cbk: what we do upon receiving reply
        """
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

    """RePCe is Hungarian for canola, http://hu.wikipedia.org/wiki/Repce

    ... also our homebrewed RPC backend where the transport layer is
    reduced to a pair of filehandles.

    This is the client component.
    """

    def __init__(self, i, o):
        self.inf, self.out = ioparse(i, o)
        self.jtab = {}
        t = Thread(target=self.listen)
        t.start()

    def listen(self):
        while True:
            select((self.inf,), (), ())
            rid, exc, res = recv(self.inf)
            rjob = self.jtab.pop(rid)
            if rjob.cbk:
                rjob.cbk(rjob, [exc, res])

    def push(self, meth, *args, **kw):
        """wrap arguments in a RepceJob, send them to server
           and return the RepceJob

           @cbk to pass on RepceJob can be given as kwarg.
        """
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
        """RePCe client is callabe, calling it implements a synchronous
        remote call.

        We do a .push with a cbk which does a wakeup upon receiving anwser,
        then wait on the RepceJob.
        """
        rjob = self.push(
            meth, *args, **{'cbk': lambda rj, res: rj.wakeup(res)})
        exc, res = rjob.wait()
        if exc:
            logging.error('call %s (%s) failed on peer with %s' %
                          (repr(rjob), meth, str(type(res).__name__)))
            raise res
        logging.debug("call %s %s -> %s" % (repr(rjob), meth, repr(res)))
        return res

    class mprx(object):

        """method proxy, standard trick to implement rubyesque
        method_missing in Python

        A class is a closure factory, you know what I mean, or go read
        some SICP.
        """

        def __init__(self, ins, meth):
            self.ins = ins
            self.meth = meth

        def __call__(self, *a):
            return self.ins(self.meth, *a)

    def __getattr__(self, meth):
        """this implements transparent method dispatch to remote object,
           so that you don't need to call the RepceClient instance like

             rclient('how_old_are_you_if_born_in', 1979)

           but you can make it into an ordinary method call like

             rclient.how_old_are_you_if_born_in(1979)
        """
        return self.mprx(self, meth)

    def __version__(self):
        """used in handshake to verify compatibility"""
        d = {'proto': self('__repce_version__')}
        try:
            d['object'] = self('version')
        except AttributeError:
            pass
        return d
