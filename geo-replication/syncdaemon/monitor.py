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
import xml.etree.ElementTree as XET
from threading import Lock
from errno import ECHILD, ESRCH
import random

from resource import SSH
import gsyncdconfig as gconf
import libgfchangelog
from rconf import rconf
from syncdutils import (select, waitpid, errno_wrap, lf, grabpidfile,
                        set_term_handler, GsyncdError,
                        Thread, finalize, Volinfo, VolinfoFromGconf,
                        gf_event, EVENT_GEOREP_FAULTY, get_up_nodes,
                        unshare_propagation_supported)
from gsyncdstatus import GeorepStatus, set_monitor_status
import py2py3
from py2py3 import pipe

ParseError = XET.ParseError if hasattr(XET, 'ParseError') else SyntaxError


def get_subvol_num(brick_idx, vol, hot):
    tier = vol.is_tier()
    disperse_count = vol.disperse_count(tier, hot)
    replica_count = vol.replica_count(tier, hot)
    distribute_count = vol.distribution_count(tier, hot)
    gconf.setconfig("primary-distribution-count", distribute_count)

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

    def monitor(self, w, argv, cpids, secondary_vol, secondary_host, primary,
                suuid, secondarynodes):
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
            self.status[w[0]['dir']] = GeorepStatus(gconf.get("state-file"),
                                                    w[0]['host'],
                                                    w[0]['dir'],
                                                    w[0]['uuid'],
                                                    primary,
                                                    "%s::%s" % (secondary_host,
                                                                secondary_vol))
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
            """ child terminated due to receipt of SIGUSR1 """
            return (os.WIFSIGNALED(s) and (os.WTERMSIG(s) == signal.SIGUSR1))

        def exit_status(s):
            if os.WIFEXITED(s):
                return os.WEXITSTATUS(s)
            return 1

        conn_timeout = gconf.get("connection-timeout")
        while ret in (0, 1):
            remote_user, remote_host = w[1][0].split("@")
            remote_id = w[1][1]
            # Check the status of the connected secondary node
            # If the connected secondary node is down then try to connect to
            # different up node.
            current_secondary_host = remote_host
            secondary_up_hosts = get_up_nodes(secondarynodes, gconf.get("ssh-port"))

            if (current_secondary_host, remote_id) not in secondary_up_hosts:
                if len(secondary_up_hosts) > 0:
                    remote_new = random.choice(secondary_up_hosts)
                    remote_host = "%s@%s" % (remote_user, remote_new[0])
                    remote_id = remote_new[1]

            # Spawn the worker in lock to avoid fd leak
            self.lock.acquire()

            self.status[w[0]['dir']].set_worker_status(self.ST_INIT)
            logging.info(lf('starting gsyncd worker',
                            brick=w[0]['dir'],
                            secondary_node=remote_host))

            pr, pw = pipe()
            cpid = os.fork()
            if cpid == 0:
                os.close(pr)

                args_to_worker = argv + [
                    'worker',
                    rconf.args.primary,
                    rconf.args.secondary,
                    '--feedback-fd', str(pw),
                    '--local-path', w[0]['dir'],
                    '--local-node', w[0]['host'],
                    '--local-node-id', w[0]['uuid'],
                    '--secondary-id', suuid,
                    '--subvol-num', str(w[2]),
                    '--resource-remote', remote_host,
                    '--resource-remote-id', remote_id
                ]

                if rconf.args.config_file is not None:
                    args_to_worker += ['-c', rconf.args.config_file]

                if w[3]:
                    args_to_worker.append("--is-hottier")

                if rconf.args.debug:
                    args_to_worker.append("--debug")

                access_mount = gconf.get("access-mount")
                if access_mount:
                    os.execv(sys.executable, args_to_worker)
                else:
                    if unshare_propagation_supported():
                        logging.debug("Worker would mount volume privately")
                        unshare_cmd = ['unshare', '-m', '--propagation',
                                       'private']
                        cmd = unshare_cmd + args_to_worker
                        os.execvp("unshare", cmd)
                    else:
                        logging.debug("Mount is not private. It would be lazy"
                                      " umounted")
                        os.execv(sys.executable, args_to_worker)

            cpids.add(cpid)
            os.close(pw)

            self.lock.release()

            t0 = time.time()
            so = select((pr,), (), (), conn_timeout)[0]
            os.close(pr)

            if so:
                ret = nwait(cpid, os.WNOHANG)

                if ret is not None:
                    logging.info(lf("worker died before establishing "
                                    "connection",
                                    brick=w[0]['dir']))
                else:
                    logging.debug("worker(%s) connected" % w[0]['dir'])
                    while time.time() < t0 + conn_timeout:
                        ret = nwait(cpid, os.WNOHANG)

                        if ret is not None:
                            logging.info(lf("worker died in startup phase",
                                            brick=w[0]['dir']))
                            break

                        time.sleep(1)
            else:
                logging.info(
                    lf("Worker not confirmed after wait, aborting it. "
                       "Gsyncd invocation on remote secondary via SSH or "
                       "gluster primary mount might have hung. Please "
                       "check the above logs for exact issue and check "
                       "primary or secondary volume for errors. Restarting "
                       "primary/secondary volume accordingly might help.",
                       brick=w[0]['dir'],
                       timeout=conn_timeout))
                errno_wrap(os.kill, [cpid, signal.SIGKILL], [ESRCH])
                ret = nwait(cpid)
            if ret is None:
                ret = nwait(cpid)
            if exit_signalled(ret):
                ret = 0
            else:
                ret = exit_status(ret)
                if ret in (0, 1):
                    self.status[w[0]['dir']].set_worker_status(self.ST_FAULTY)
                    gf_event(EVENT_GEOREP_FAULTY,
                             primary_volume=primary.volume,
                             primary_node=w[0]['host'],
                             primary_node_id=w[0]['uuid'],
                             secondary_host=secondary_host,
                             secondary_volume=secondary_vol,
                             current_secondary_host=current_secondary_host,
                             brick_path=w[0]['dir'])
            time.sleep(10)
        self.status[w[0]['dir']].set_worker_status(self.ST_INCON)
        return ret

    def multiplex(self, wspx, suuid, secondary_vol, secondary_host, primary, secondarynodes):
        argv = [os.path.basename(sys.executable), sys.argv[0]]

        cpids = set()
        ta = []
        for wx in wspx:
            def wmon(w):
                cpid, _ = self.monitor(w, argv, cpids, secondary_vol,
                                       secondary_host, primary, suuid, secondarynodes)
                time.sleep(1)
                self.lock.acquire()
                for cpid in cpids:
                    errno_wrap(os.kill, [cpid, signal.SIGKILL], [ESRCH])
                self.lock.release()
                finalize(exval=1)
            t = Thread(target=wmon, args=[wx])
            t.start()
            ta.append(t)

        # monitor status was being updated in each monitor thread. It
        # should not be done as it can cause deadlock for a worker start.
        # set_monitor_status uses flock to synchronize multple instances
        # updating the file. Since each monitor thread forks worker,
        # these processes can hold the reference to fd of status
        # file causing deadlock to workers which starts later as flock
        # will not be release until all references to same fd is closed.
        # It will also cause fd leaks.

        self.lock.acquire()
        set_monitor_status(gconf.get("state-file"), self.ST_STARTED)
        self.lock.release()
        for t in ta:
            t.join()


def distribute(primary, secondary):
    if rconf.args.use_gconf_volinfo:
        mvol = VolinfoFromGconf(primary.volume, primary=True)
    else:
        mvol = Volinfo(primary.volume, primary.host, primary=True)
    logging.debug('primary bricks: ' + repr(mvol.bricks))
    prelude = []
    secondary_host = None
    secondary_vol = None

    prelude = [gconf.get("ssh-command")] + \
        gconf.get("ssh-options").split() + \
        ["-p", str(gconf.get("ssh-port"))] + \
        [secondary.remote_addr]

    logging.debug('secondary SSH gateway: ' + secondary.remote_addr)

    if rconf.args.use_gconf_volinfo:
        svol = VolinfoFromGconf(secondary.volume, primary=False)
    else:
        svol = Volinfo(secondary.volume, "localhost", prelude, primary=False)

    sbricks = svol.bricks
    suuid = svol.uuid
    secondary_host = secondary.remote_addr.split('@')[-1]
    secondary_vol = secondary.volume

    # save this xattr for the session delete command
    old_stime_xattr_prefix = gconf.get("stime-xattr-prefix", None)
    new_stime_xattr_prefix = "trusted.glusterfs." + mvol.uuid + "." + \
                             svol.uuid
    if not old_stime_xattr_prefix or \
       old_stime_xattr_prefix != new_stime_xattr_prefix:
        gconf.setconfig("stime-xattr-prefix", new_stime_xattr_prefix)

    logging.debug('secondary bricks: ' + repr(sbricks))

    secondarynodes = set((b['host'], b["uuid"]) for b in sbricks)
    rap = SSH.parse_ssh_address(secondary)
    secondarys = [(rap['user'] + '@' + h[0], h[1]) for h in secondarynodes]

    workerspex = []
    for idx, brick in enumerate(mvol.bricks):
        if rconf.args.local_node_id == brick['uuid']:
            is_hot = mvol.is_hot(":".join([brick['host'], brick['dir']]))
            workerspex.append((brick,
                               secondarys[idx % len(secondarys)],
                               get_subvol_num(idx, mvol, is_hot),
                               is_hot))
    logging.debug('worker specs: ' + repr(workerspex))
    return workerspex, suuid, secondary_vol, secondary_host, primary, secondarynodes


def monitor(local, remote):
    # Check if gsyncd restarted in pause state. If
    # yes, send SIGSTOP to negative of monitor pid
    # to go back to pause state.
    if rconf.args.pause_on_start:
        errno_wrap(os.kill, [-os.getpid(), signal.SIGSTOP], [ESRCH])

    """oh yeah, actually Monitor is used as singleton, too"""
    return Monitor().multiplex(*distribute(local, remote))


def startup(go_daemon=True):
    """set up logging, pidfile grabbing, daemonization"""
    pid_file = gconf.get("pid-file")
    if not grabpidfile():
        sys.stderr.write("pidfile is taken, exiting.\n")
        sys.exit(2)
    rconf.pid_file_owned = True

    if not go_daemon:
        return

    x, y = pipe()
    cpid = os.fork()
    if cpid:
        os.close(x)
        sys.exit()
    os.close(y)
    os.setsid()
    dn = os.open(os.devnull, os.O_RDWR)
    for f in (sys.stdin, sys.stdout, sys.stderr):
        os.dup2(dn, f.fileno())

    if not grabpidfile(pid_file + '.tmp'):
        raise GsyncdError("cannot grab temporary pidfile")

    os.rename(pid_file + '.tmp', pid_file)

    # wait for parent to terminate
    # so we can start up with
    # no messing from the dirty
    # ol' bustard
    select((x,), (), ())
    os.close(x)
