import os
import sys
import time
import signal
import logging
from gconf import gconf
from syncdutils import update_file, select, waitpid

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
        def sigcont_handler(*a):
            """
            Re-init logging and send group kill signal
            """
            md = gconf.log_metadata
            logging.shutdown()
            lcls = logging.getLoggerClass()
            lcls.setup(label=md.get('saved_label'), **md)
            pid = os.getpid()
            os.kill(-pid, signal.SIGUSR1)
        signal.signal(signal.SIGUSR1, lambda *a: ())
        signal.signal(signal.SIGCONT, sigcont_handler)

        argv = sys.argv[:]
        for o in ('-N', '--no-daemon', '--monitor'):
            while o in argv:
                argv.remove(o)
        argv.extend(('-N', '-p', ''))
        argv.insert(0, os.path.basename(sys.executable))

        self.set_state('starting...')
        ret = 0
        def nwait(p, o=0):
            p2, r = waitpid(p, o)
            if not p2:
                return
            return r
        def exit_signalled(s):
            """ child teminated due to receipt of SIGUSR1 """
            return (os.WIFSIGNALED(s) and (os.WTERMSIG(s) == signal.SIGUSR1))
        def exit_status(s):
            if os.WIFEXITED(s):
                return os.WEXITSTATUS(s)
            return 1
        conn_timeout = int(gconf.connection_timeout)
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
            so = select((pr,), (), (), conn_timeout)[0]
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
                os.kill(cpid, signal.SIGKILL)
                ret = nwait(cpid)
            if ret == None:
                self.set_state('OK')
                ret = nwait(cpid)
            if exit_signalled(ret):
                ret = 0
            else:
                ret = exit_status(ret)
                if ret in (0,1):
                    self.set_state('faulty')
            time.sleep(10)
        self.set_state('inconsistent')
        return ret

def monitor():
    return Monitor().monitor()
