import os
import sys
import time
import logging
import select
from signal import SIGKILL
from gconf import gconf
from syncdutils import update_file

class Monitor(object):
    """class which spawns and manages gsyncd workers"""

    def __init__(self):
        self.state = None

    def set_state(self, state):
        """set the state that can be used by external agents
           like glusterd for status reporting"""
        if state == self.state:
            return
        self.state = state
        logging.info('new state: %s' % state)
        if getattr(gconf, 'state_file', None):
            update_file(gconf.state_file, lambda f: f.write(state + '\n'))

    def monitor(self):
        """the monitor loop

        Basic logic is a blantantly simple blunt heuristics:
        if spawned client survives 60 secs, it's considered OK.
        This servers us pretty well as it's not vulneralbe to
        any kind of irregular behavior of the child...

        ... well, except for one: if children is hung up on
        waiting for some event, it can survive aeons, still
        will be defunct. So we tweak the above logic to
        expect the worker to send us a signal within 60 secs
        (in the form of closing its end of a pipe). The worker
        does this when it's done with the setup stage
        ready to enter the service loop (note it's the setup
        stage which is vulnerable to hangs -- the full
        blown worker blows up on EPIPE if the net goes down,
        due to the keep-alive thread)
        """
        argv = sys.argv[:]
        for o in ('-N', '--no-daemon', '--monitor'):
            while o in argv:
                argv.remove(o)
        argv.extend(('-N', '-p', ''))
        argv.insert(0, os.path.basename(sys.executable))

        self.set_state('starting...')
        ret = 0
        def nwait(p, o=0):
            p2, r = os.waitpid(p, o)
            if not p2:
                return
            if os.WIFEXITED(r):
                return os.WEXITSTATUS(r)
            return 1
        conn_timeout = 60
        while ret in (0, 1):
            logging.info('-' * conn_timeout)
            logging.info('starting gsyncd worker')
            pr, pw = os.pipe()
            cpid = os.fork()
            if cpid == 0:
                os.close(pr)
                os.execv(sys.executable, argv + ['--feedback-fd', str(pw)])
            os.close(pw)
            t0 = time.time()
            so = select.select((pr,), (), (), conn_timeout)[0]
            os.close(pr)
            if so:
                ret = nwait(cpid, os.WNOHANG)
                if ret != None:
                    logging.debug("worker died before establishing connection")
                else:
                    logging.debug("worker seems to be connected (?? racy check)")
                    while time.time() < t0 + conn_timeout:
                        ret = nwait(cpid, os.WNOHANG)
                        if ret != None:
                            logging.debug("worker died in startup phase")
                            break
                        time.sleep(1)
            else:
                logging.debug("worker not confirmed in %d sec, aborting it" % \
                              conn_timeout)
                os.kill(cpid, SIGKILL)
                ret = nwait(cpid)
            if ret == None:
                self.set_state('OK')
                ret = nwait(cpid)
            elif ret in (0, 1):
                self.set_state('faulty')
            time.sleep(10)
        self.set_state('inconsistent')
        return ret

def monitor():
    """oh yeah, actually Monitor is used as singleton, too"""
    return Monitor().monitor()
