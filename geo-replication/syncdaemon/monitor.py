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
from errno import ECHILD, ESRCH
import re
import random
from gconf import gconf
from syncdutils import select, waitpid, errno_wrap
from syncdutils import set_term_handler, is_host_local, GsyncdError
from syncdutils import escape, Thread, finalize, memoize
from syncdutils import gf_event, EVENT_GEOREP_FAULTY

from gsyncdstatus import GeorepStatus, set_monitor_status


ParseError = XET.ParseError if hasattr(XET, 'ParseError') else SyntaxError


def get_subvol_num(brick_idx, vol, hot):
    tier = vol.is_tier()
    disperse_count = vol.disperse_count(tier, hot)
    replica_count = vol.replica_count(tier, hot)

    if (tier and not hot):
        brick_idx = brick_idx - vol.get_hot_bricks_count(tier)

    subvol_size = disperse_count if disperse_count > 0 else replica_count
    cnt = int((brick_idx + 1) / subvol_size)
    rem = (brick_idx + 1) % subvol_size
    if rem > 0:
        cnt = cnt + 1

    if (tier and hot):
        return "hot_" + str(cnt)
    elif (tier and not hot):
        return "cold_" + str(cnt)
    else:
        return str(cnt)


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
                              'failed with errorcode %s' %
                              (vol, via, vi.find('opErrno').text))
        self.tree = vi
        self.volume = vol
        self.host = host

    def get(self, elem):
        return self.tree.findall('.//' + elem)

    def is_tier(self):
        return (self.get('typeStr')[0].text == 'Tier')

    def is_hot(self, brickpath):
        logging.debug('brickpath: ' + repr(brickpath))
        return brickpath in self.hot_bricks

    @property
    @memoize
    def bricks(self):
        def bparse(b):
            host, dirp = b.find("name").text.split(':', 2)
            return {'host': host, 'dir': dirp, 'uuid': b.find("hostUuid").text}
        return [bparse(b) for b in self.get('brick')]

    @property
    @memoize
    def uuid(self):
        ids = self.get('id')
        if len(ids) != 1:
            raise GsyncdError("volume info of %s obtained from %s: "
                              "ambiguous uuid" % (self.volume, self.host))
        return ids[0].text

    def replica_count(self, tier, hot):
        if (tier and hot):
            return int(self.get('hotBricks/hotreplicaCount')[0].text)
        elif (tier and not hot):
            return int(self.get('coldBricks/coldreplicaCount')[0].text)
        else:
            return int(self.get('replicaCount')[0].text)

    def disperse_count(self, tier, hot):
        if (tier and hot):
            # Tiering doesn't support disperse volume as hot brick,
            # hence no xml output, so returning 0. In case, if it's
            # supported later, we should change here.
            return 0
        elif (tier and not hot):
            return int(self.get('coldBricks/colddisperseCount')[0].text)
        else:
            return int(self.get('disperseCount')[0].text)

    @property
    @memoize
    def hot_bricks(self):
        return [b.text for b in self.get('hotBricks/brick')]

    def get_hot_bricks_count(self, tier):
        if (tier):
            return int(self.get('hotBricks/hotbrickCount')[0].text)
        else:
            return 0


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
        errno_wrap(os.kill, [-os.getpid(), signal.SIGTERM], [ESRCH])

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
        if not self.status.get(w[0]['dir'], None):
            self.status[w[0]['dir']] = GeorepStatus(gconf.state_file,
                                                    w[0]['host'],
                                                    w[0]['dir'],
                                                    w[0]['uuid'],
                                                    master,
                                                    "%s::%s" % (slave_host,
                                                                slave_vol))

        set_monitor_status(gconf.state_file, self.ST_STARTED)
        self.status[w[0]['dir']].set_worker_status(self.ST_INIT)

        ret = 0

        def nwait(p, o=0):
            try:
                p2, r = waitpid(p, o)
                if not p2:
                    return
                return r
            except OSError as e:
                # no child process, this happens if the child process
                # already died and has been cleaned up
                if e.errno == ECHILD:
                    return -1
                else:
                    raise

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

            logging.info('starting gsyncd worker(%s). Slave node: %s' %
                         (w[0]['dir'], remote_host))

            # Couple of pipe pairs for RPC communication b/w
            # worker and changelog agent.

            # read/write end for agent
            (ra, ww) = os.pipe()
            # read/write end for worker
            (rw, wa) = os.pipe()

            # spawn the agent process
            apid = os.fork()
            if apid == 0:
                os.close(rw)
                os.close(ww)
                os.execv(sys.executable, argv + ['--local-path', w[0]['dir'],
                                                 '--local-node', w[0]['host'],
                                                 '--local-node-id',
                                                 w[0]['uuid'],
                                                 '--agent',
                                                 '--rpc-fd',
                                                 ','.join([str(ra), str(wa),
                                                           str(rw), str(ww)])])
            pr, pw = os.pipe()
            cpid = os.fork()
            if cpid == 0:
                os.close(pr)
                os.close(ra)
                os.close(wa)
                os.execv(sys.executable, argv + ['--feedback-fd', str(pw),
                                                 '--local-path', w[0]['dir'],
                                                 '--local-node', w[0]['host'],
                                                 '--local-node-id',
                                                 w[0]['uuid'],
                                                 '--local-id',
                                                 '.' + escape(w[0]['dir']),
                                                 '--rpc-fd',
                                                 ','.join([str(rw), str(ww),
                                                           str(ra), str(wa)]),
                                                 '--subvol-num', str(w[2])] +
                         (['--is-hottier'] if w[3] else []) +
                         ['--resource-remote', remote_host])

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
                ret_agent = nwait(apid, os.WNOHANG)

                if ret_agent is not None:
                    # Agent is died Kill Worker
                    logging.info("Changelog Agent died, "
                                 "Aborting Worker(%s)" % w[0]['dir'])
                    errno_wrap(os.kill, [cpid, signal.SIGKILL], [ESRCH])
                    nwait(cpid)
                    nwait(apid)

                if ret is not None:
                    logging.info("worker(%s) died before establishing "
                                 "connection" % w[0]['dir'])
                    nwait(apid)  # wait for agent
                else:
                    logging.debug("worker(%s) connected" % w[0]['dir'])
                    while time.time() < t0 + conn_timeout:
                        ret = nwait(cpid, os.WNOHANG)
                        ret_agent = nwait(apid, os.WNOHANG)

                        if ret is not None:
                            logging.info("worker(%s) died in startup "
                                         "phase" % w[0]['dir'])
                            nwait(apid)  # wait for agent
                            break

                        if ret_agent is not None:
                            # Agent is died Kill Worker
                            logging.info("Changelog Agent died, Aborting "
                                         "Worker(%s)" % w[0]['dir'])
                            errno_wrap(os.kill, [cpid, signal.SIGKILL], [ESRCH])
                            nwait(cpid)
                            nwait(apid)
                            break

                        time.sleep(1)
            else:
                logging.info("worker(%s) not confirmed in %d sec, "
                             "aborting it" % (w[0]['dir'], conn_timeout))
                errno_wrap(os.kill, [cpid, signal.SIGKILL], [ESRCH])
                nwait(apid)  # wait for agent
                ret = nwait(cpid)
            if ret is None:
                self.status[w[0]['dir']].set_worker_status(self.ST_STABLE)
                # If worker dies, agent terminates on EOF.
                # So lets wait for agent first.
                nwait(apid)
                ret = nwait(cpid)
            if exit_signalled(ret):
                ret = 0
            else:
                ret = exit_status(ret)
                if ret in (0, 1):
                    self.status[w[0]['dir']].set_worker_status(self.ST_FAULTY)
                    gf_event(EVENT_GEOREP_FAULTY,
                             master_volume=master.volume,
                             master_node=w[0]['host'],
                             master_node_id=w[0]['uuid'],
                             slave_host=slave_host,
                             slave_volume=slave_vol,
                             current_slave_host=current_slave_host,
                             brick_path=w[0]['dir'])
            time.sleep(10)
        self.status[w[0]['dir']].set_worker_status(self.ST_INCON)
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
                    errno_wrap(os.kill, [cpid, signal.SIGKILL], [ESRCH])
                for apid in agents:
                    errno_wrap(os.kill, [apid, signal.SIGKILL], [ESRCH])
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

        # save this xattr for the session delete command
        old_stime_xattr_name = getattr(gconf, "master.stime_xattr_name", None)
        new_stime_xattr_name = "trusted.glusterfs." + mvol.uuid + "." + \
            svol.uuid + ".stime"
        if not old_stime_xattr_name or \
           old_stime_xattr_name != new_stime_xattr_name:
            gconf.configinterface.set("master.stime_xattr_name",
                                      new_stime_xattr_name)
    else:
        raise GsyncdError("unknown slave type " + slave.url)
    logging.debug('slave bricks: ' + repr(sbricks))
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

    workerspex = []
    for idx, brick in enumerate(mvol.bricks):
        if is_host_local(brick['uuid']):
            is_hot = mvol.is_hot(":".join([brick['host'], brick['dir']]))
            workerspex.append((brick,
                               slaves[idx % len(slaves)],
                               get_subvol_num(idx, mvol, is_hot),
                               is_hot))
    logging.debug('worker specs: ' + repr(workerspex))
    return workerspex, suuid, slave_vol, slave_host, master


def monitor(*resources):
    # Check if gsyncd restarted in pause state. If
    # yes, send SIGSTOP to negative of monitor pid
    # to go back to pause state.
    if gconf.pause_on_start:
        errno_wrap(os.kill, [-os.getpid(), signal.SIGSTOP], [ESRCH])

    """oh yeah, actually Monitor is used as singleton, too"""
    return Monitor().multiplex(*distribute(*resources))
