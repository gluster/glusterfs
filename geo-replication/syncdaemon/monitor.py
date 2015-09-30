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
import signal
import logging
import uuid
import xml.etree.ElementTree as XET
from subprocess import PIPE
from resource import Popen, FILE, GLUSTER, SSH
from threading import Lock
from errno import EEXIST
import re
import random
from gconf import gconf
from syncdutils import select, waitpid
from syncdutils import set_term_handler, is_host_local, GsyncdError
from syncdutils import escape, Thread, finalize, memoize

from gsyncdstatus import GeorepStatus, set_monitor_status


ParseError = XET.ParseError if hasattr(XET, 'ParseError') else SyntaxError


def get_subvol_num(brick_idx, replica_count, disperse_count):
    subvol_size = disperse_count if disperse_count > 0 else replica_count
    cnt = int((brick_idx + 1) / subvol_size)
    rem = (brick_idx + 1) % subvol_size
    if rem > 0:
        return cnt + 1
    else:
        return cnt


def get_slave_bricks_status(host, vol):
    po = Popen(['gluster', '--xml', '--remote-host=' + host,
                'volume', 'status', vol, "detail"],
               stdout=PIPE, stderr=PIPE)
    vix = po.stdout.read()
    po.wait()
    po.terminate_geterr(fail_on_err=False)
    if po.returncode != 0:
        logging.info("Volume status command failed, unable to get "
                     "list of up nodes of %s, returning empty list: %s" %
                     (vol, po.returncode))
        return []
    vi = XET.fromstring(vix)
    if vi.find('opRet').text != '0':
        logging.info("Unable to get list of up nodes of %s, "
                     "returning empty list: %s" %
                     (vol, vi.find('opErrstr').text))
        return []

    up_hosts = set()

    try:
        for el in vi.findall('volStatus/volumes/volume/node'):
            if el.find('status').text == '1':
                up_hosts.add(el.find('hostname').text)
    except (ParseError, AttributeError, ValueError) as e:
        logging.info("Parsing failed to get list of up nodes of %s, "
                     "returning empty list: %s" % (vol, e))

    return list(up_hosts)


class Volinfo(object):

    def __init__(self, vol, host='localhost', prelude=[]):
        po = Popen(prelude + ['gluster', '--xml', '--remote-host=' + host,
                              'volume', 'info', vol],
                   stdout=PIPE, stderr=PIPE)
        vix = po.stdout.read()
        po.wait()
        po.terminate_geterr()
        vi = XET.fromstring(vix)
        if vi.find('opRet').text != '0':
            if prelude:
                via = '(via %s) ' % prelude.join(' ')
            else:
                via = ' '
            raise GsyncdError('getting volume info of %s%s '
                              'failed with errorcode %s',
                              (vol, via, vi.find('opErrno').text))
        self.tree = vi
        self.volume = vol
        self.host = host

    def get(self, elem):
        return self.tree.findall('.//' + elem)

    @property
    @memoize
    def bricks(self):
        def bparse(b):
            host, dirp = b.text.split(':', 2)
            return {'host': host, 'dir': dirp}
        return [bparse(b) for b in self.get('brick')]

    @property
    @memoize
    def uuid(self):
        ids = self.get('id')
        if len(ids) != 1:
            raise GsyncdError("volume info of %s obtained from %s: "
                              "ambiguous uuid",
                              self.volume, self.host)
        return ids[0].text

    @property
    @memoize
    def replica_count(self):
        return int(self.get('replicaCount')[0].text)

    @property
    @memoize
    def disperse_count(self):
        return int(self.get('disperseCount')[0].text)


class Monitor(object):

    """class which spawns and manages gsyncd workers"""

    ST_INIT = 'Initializing...'
    ST_STARTED = 'Started'
    ST_STABLE = 'Active'
    ST_FAULTY = 'Faulty'
    ST_INCON = 'inconsistent'
    _ST_ORD = [ST_STABLE, ST_INIT, ST_FAULTY, ST_INCON]

    def __init__(self):
        self.lock = Lock()
        self.state = {}
        self.status = {}

    @staticmethod
    def terminate():
        # relax one SIGTERM by setting a handler that sets back
        # standard handler
        set_term_handler(lambda *a: set_term_handler())
        # give a chance to graceful exit
        os.kill(-os.getpid(), signal.SIGTERM)

    def monitor(self, w, argv, cpids, agents, slave_vol, slave_host, master):
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
        if not self.status.get(w[0], None):
            self.status[w[0]] = GeorepStatus(gconf.state_file, w[0])

        set_monitor_status(gconf.state_file, self.ST_STARTED)
        self.status[w[0]].set_worker_status(self.ST_INIT)

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
            remote_host = w[1]
            # Check the status of the connected slave node
            # If the connected slave node is down then try to connect to
            # different up node.
            m = re.match("(ssh|gluster|file):\/\/(.+)@([^:]+):(.+)",
                         remote_host)
            if m:
                current_slave_host = m.group(3)
                slave_up_hosts = get_slave_bricks_status(
                    slave_host, slave_vol)

                if current_slave_host not in slave_up_hosts:
                    if len(slave_up_hosts) > 0:
                        remote_host = "%s://%s@%s:%s" % (m.group(1),
                                                         m.group(2),
                                                         random.choice(
                                                             slave_up_hosts),
                                                         m.group(4))

            # Spawn the worker and agent in lock to avoid fd leak
            self.lock.acquire()

            logging.info('-' * conn_timeout)
            logging.info('starting gsyncd worker')

            # Couple of pipe pairs for RPC communication b/w
            # worker and changelog agent.

            # read/write end for agent
            (ra, ww) = os.pipe()
            # read/write end for worker
            (rw, wa) = os.pipe()

            # spawn the agent process
            apid = os.fork()
            if apid == 0:
                os.execv(sys.executable, argv + ['--local-path', w[0],
                                                 '--agent',
                                                 '--rpc-fd',
                                                 ','.join([str(ra), str(wa),
                                                           str(rw), str(ww)])])
            pr, pw = os.pipe()
            cpid = os.fork()
            if cpid == 0:
                os.close(pr)
                os.execv(sys.executable, argv + ['--feedback-fd', str(pw),
                                                 '--local-path', w[0],
                                                 '--local-id',
                                                 '.' + escape(w[0]),
                                                 '--rpc-fd',
                                                 ','.join([str(rw), str(ww),
                                                           str(ra), str(wa)]),
                                                 '--subvol-num', str(w[2]),
                                                 '--resource-remote',
                                                 remote_host])

            cpids.add(cpid)
            agents.add(apid)
            os.close(pw)

            # close all RPC pipes in monitor
            os.close(ra)
            os.close(wa)
            os.close(rw)
            os.close(ww)
            self.lock.release()

            t0 = time.time()
            so = select((pr,), (), (), conn_timeout)[0]
            os.close(pr)

            if so:
                ret = nwait(cpid, os.WNOHANG)
                if ret is not None:
                    logging.info("worker(%s) died before establishing "
                                 "connection" % w[0])
                    nwait(apid) #wait for agent
                else:
                    logging.debug("worker(%s) connected" % w[0])
                    while time.time() < t0 + conn_timeout:
                        ret = nwait(cpid, os.WNOHANG)
                        if ret is not None:
                            logging.info("worker(%s) died in startup "
                                         "phase" % w[0])
                            nwait(apid) #wait for agent
                            break
                        time.sleep(1)
            else:
                logging.info("worker(%s) not confirmed in %d sec, "
                             "aborting it" % (w[0], conn_timeout))
                os.kill(cpid, signal.SIGKILL)
                nwait(apid) #wait for agent
                ret = nwait(cpid)
            if ret is None:
                self.status[w[0]].set_worker_status(self.ST_STABLE)
                #If worker dies, agent terminates on EOF.
                #So lets wait for agent first.
                nwait(apid)
                ret = nwait(cpid)
            if exit_signalled(ret):
                ret = 0
            else:
                ret = exit_status(ret)
                if ret in (0, 1):
                    self.status[w[0]].set_worker_status(self.ST_FAULTY)
            time.sleep(10)
        self.status[w[0]].set_worker_status(self.ST_INCON)
        return ret

    def multiplex(self, wspx, suuid, slave_vol, slave_host, master):
        argv = sys.argv[:]
        for o in ('-N', '--no-daemon', '--monitor'):
            while o in argv:
                argv.remove(o)
        argv.extend(('-N', '-p', '', '--slave-id', suuid))
        argv.insert(0, os.path.basename(sys.executable))

        cpids = set()
        agents = set()
        ta = []
        for wx in wspx:
            def wmon(w):
                cpid, _ = self.monitor(w, argv, cpids, agents, slave_vol,
                                       slave_host, master)
                time.sleep(1)
                self.lock.acquire()
                for cpid in cpids:
                    os.kill(cpid, signal.SIGKILL)
                for apid in agents:
                    os.kill(apid, signal.SIGKILL)
                self.lock.release()
                finalize(exval=1)
            t = Thread(target=wmon, args=[wx])
            t.start()
            ta.append(t)
        for t in ta:
            t.join()


def distribute(*resources):
    master, slave = resources
    mvol = Volinfo(master.volume, master.host)
    logging.debug('master bricks: ' + repr(mvol.bricks))
    prelude = []
    si = slave
    slave_host = None
    slave_vol = None

    if isinstance(slave, SSH):
        prelude = gconf.ssh_command.split() + [slave.remote_addr]
        si = slave.inner_rsc
        logging.debug('slave SSH gateway: ' + slave.remote_addr)
    if isinstance(si, FILE):
        sbricks = {'host': 'localhost', 'dir': si.path}
        suuid = uuid.uuid5(uuid.NAMESPACE_URL, slave.get_url(canonical=True))
    elif isinstance(si, GLUSTER):
        svol = Volinfo(si.volume, slave.remote_addr.split('@')[-1])
        sbricks = svol.bricks
        suuid = svol.uuid
        slave_host = slave.remote_addr.split('@')[-1]
        slave_vol = si.volume
    else:
        raise GsyncdError("unknown slave type " + slave.url)
    logging.info('slave bricks: ' + repr(sbricks))
    if isinstance(si, FILE):
        slaves = [slave.url]
    else:
        slavenodes = set(b['host'] for b in sbricks)
        if isinstance(slave, SSH) and not gconf.isolated_slave:
            rap = SSH.parse_ssh_address(slave)
            slaves = ['ssh://' + rap['user'] + '@' + h + ':' + si.url
                      for h in slavenodes]
        else:
            slavevols = [h + ':' + si.volume for h in slavenodes]
            if isinstance(slave, SSH):
                slaves = ['ssh://' + rap.remote_addr + ':' + v
                          for v in slavevols]
            else:
                slaves = slavevols

    workerspex = [(brick['dir'], slaves[idx % len(slaves)],
                  get_subvol_num(idx, mvol.replica_count, mvol.disperse_count))
                  for idx, brick in enumerate(mvol.bricks)
                  if is_host_local(brick['host'])]
    logging.info('worker specs: ' + repr(workerspex))
    return workerspex, suuid, slave_vol, slave_host, master


def monitor(*resources):
    # Check if gsyncd restarted in pause state. If
    # yes, send SIGSTOP to negative of monitor pid
    # to go back to pause state.
    if gconf.pause_on_start:
        os.kill(-os.getpid(), signal.SIGSTOP)

    """oh yeah, actually Monitor is used as singleton, too"""
    return Monitor().multiplex(*distribute(*resources))
