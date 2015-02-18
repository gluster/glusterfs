#!/usr/bin/env python

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import sys
import socket
from subprocess import PIPE, Popen
from errno import EPERM, EEXIST
import logging
import os
from datetime import datetime

ROOT_GFID = "00000000-0000-0000-0000-000000000001"


def find(path, callback_func=lambda x: True, filter_func=lambda x: True,
         ignore_dirs=[], subdirs_crawl=True):
    if os.path.basename(path) in ignore_dirs:
        return

    if filter_func(path):
        callback_func(path)

    for p in os.listdir(path):
        full_path = os.path.join(path, p)

        if os.path.isdir(full_path):
            if subdirs_crawl:
                find(full_path, callback_func, filter_func, ignore_dirs)
            else:
                if filter_func(full_path):
                    callback_func(full_path)
        else:
            if filter_func(full_path):
                callback_func(full_path)


def output_write(f, path, prefix="."):
    if path == "":
        return

    if prefix != ".":
        path = os.path.join(prefix, path)
    f.write("%s\n" % path)


def human_time(ts):
    return datetime.fromtimestamp(float(ts)).strftime("%Y-%m-%d %H:%M:%S")


def setup_logger(logger, path, debug=False):
    if debug:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    # create the logging file handler
    fh = logging.FileHandler(path)

    formatter = logging.Formatter("[%(asctime)s] %(levelname)s "
                                  "[%(module)s - %(lineno)s:%(funcName)s] "
                                  "- %(message)s")

    fh.setFormatter(formatter)

    # add handler to logger object
    logger.addHandler(fh)


def create_file(path, exit_on_err=False, logger=None):
    """
    If file exists overwrite. Print error to stderr and exit
    if exit_on_err is set, else raise the exception. Consumer
    should handle the exception.
    """
    try:
        open(path, 'w').close()
    except (OSError, IOError) as e:
        if exit_on_err:
            fail("Failed to create file %s: %s" % (path, e), logger=logger)
        else:
            raise


def mkdirp(path, exit_on_err=False, logger=None):
    """
    Try creating required directory structure
    ignore EEXIST and raise exception for rest of the errors.
    Print error in stderr and exit if exit_on_err is set, else
    raise exception.
    """
    try:
        os.makedirs(path)
    except (OSError, IOError) as e:
        if e.errno == EEXIST and os.path.isdir(path):
            pass
        else:
            if exit_on_err:
                fail("Fail to create dir %s: %s" % (path, e), logger=logger)
            else:
                raise


def fail(msg, code=1, logger=None):
    """
    Write error to stderr and exit
    """
    if logger:
        logger.error(msg)
    sys.stderr.write("%s\n" % msg)
    sys.exit(code)


def execute(cmd, exit_msg=None, logger=None):
    """
    If failure_msg is not None then return returncode, out and error.
    If failure msg is set, write to stderr and exit.
    """
    p = Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=PIPE, close_fds=True)

    (out, err) = p.communicate()
    if p.returncode != 0 and exit_msg is not None:
        fail("%s: %s" % (exit_msg, err), p.returncode, logger=logger)

    return (p.returncode, out, err)


def symlink_gfid_to_path(brick, gfid):
    """
    Each directories are symlinked to file named GFID
    in .glusterfs directory of brick backend. Using readlink
    we get PARGFID/basename of dir. readlink recursively till
    we get PARGFID as ROOT_GFID.
    """
    if gfid == ROOT_GFID:
        return ""

    out_path = ""
    while True:
        path = os.path.join(brick, ".glusterfs", gfid[0:2], gfid[2:4], gfid)
        path_readlink = os.readlink(path)
        pgfid = os.path.dirname(path_readlink)
        out_path = os.path.join(os.path.basename(path_readlink), out_path)
        if pgfid == "../../00/00/%s" % ROOT_GFID:
            break
        gfid = os.path.basename(pgfid)
    return out_path


def is_host_local(host):
    """
    Find if a host is local or not.
    Code copied from $GLUSTERFS/geo-replication/syncdaemon/syncdutils.py
    """
    locaddr = False
    for ai in socket.getaddrinfo(host, None):
        # cf. http://github.com/gluster/glusterfs/blob/ce111f47/xlators
        # /mgmt/glusterd/src/glusterd-utils.c#L125
        if ai[0] == socket.AF_INET:
            if ai[-1][0].split(".")[0] == "127":
                locaddr = True
                break
        elif ai[0] == socket.AF_INET6:
            if ai[-1][0] == "::1":
                locaddr = True
                break
        else:
            continue
        try:
            # use ICMP socket to avoid net.ipv4.ip_nonlocal_bind issue,
            # cf. https://bugzilla.redhat.com/show_bug.cgi?id=890587
            s = socket.socket(ai[0], socket.SOCK_RAW, socket.IPPROTO_ICMP)
        except socket.error:
            ex = sys.exc_info()[1]
            if ex.errno != EPERM:
                raise
            f = None
            try:
                f = open("/proc/sys/net/ipv4/ip_nonlocal_bind")
                if int(f.read()) != 0:
                    logger.warning("non-local bind is set and not "
                                   "allowed to create "
                                   "raw sockets, cannot determine "
                                   "if %s is local" % host)
                    return False
                s = socket.socket(ai[0], socket.SOCK_DGRAM)
            finally:
                if f:
                    f.close()
        try:
            s.bind(ai[-1])
            locaddr = True
            break
        except:
            pass
        s.close()
    return locaddr
