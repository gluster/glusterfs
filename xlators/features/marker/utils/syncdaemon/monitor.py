import os
import sys
import time
import logging
from gconf import gconf
from syncdutils import update_file

class Monitor(object):

    def __init__(self):
        self.state = None

    def set_state(self, state):
        if state == self.state:
            return
        self.state = state
        logging.info('new state: %s' % state)
        if getattr(gconf, 'state_file', None):
            update_file(gconf.state_file, lambda f: f.write(state + '\n'))

    def monitor(self):
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
        while ret in (0, 1):
            logging.info('-' * 60)
            logging.info('starting gsyncd worker')
            cpid = os.spawnv(os.P_NOWAIT, sys.executable, argv)
            time.sleep(60)
            ret = nwait(cpid, os.WNOHANG)
            if ret == None:
                self.set_state('OK')
                ret = nwait(cpid)
            elif ret in (0, 1):
                self.set_state('faulty')
            time.sleep(10)
        self.set_state('inconsistent')
        return ret

def monitor():
    return Monitor().monitor()
