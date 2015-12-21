#!/usr/bin/env python
#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
import logging
import syncdutils
from syncdutils import select, CHANGELOG_AGENT_SERVER_VERSION
from repce import RepceServer


class _MetaChangelog(object):

    def __getattr__(self, meth):
        from libgfchangelog import Changes as LChanges
        xmeth = [m for m in dir(LChanges) if m[0] != '_']
        if meth not in xmeth:
            return
        for m in xmeth:
            setattr(self, m, getattr(LChanges, m))
        return getattr(self, meth)

Changes = _MetaChangelog()


class Changelog(object):
    def version(self):
        return CHANGELOG_AGENT_SERVER_VERSION

    def init(self):
        return Changes.cl_init()

    def register(self, cl_brick, cl_dir, cl_log, cl_level, retries=0):
        return Changes.cl_register(cl_brick, cl_dir, cl_log, cl_level, retries)

    def scan(self):
        return Changes.cl_scan()

    def getchanges(self):
        return Changes.cl_getchanges()

    def done(self, clfile):
        return Changes.cl_done(clfile)

    def history(self, changelog_path, start, end, num_parallel):
        return Changes.cl_history_changelog(changelog_path, start, end,
                                            num_parallel)

    def history_scan(self):
        return Changes.cl_history_scan()

    def history_getchanges(self):
        return Changes.cl_history_getchanges()

    def history_done(self, clfile):
        return Changes.cl_history_done(clfile)


class ChangelogAgent(object):
    def __init__(self, obj, fd_tup):
        (inf, ouf, rw, ww) = fd_tup.split(',')
        repce = RepceServer(obj, int(inf), int(ouf), 1)
        t = syncdutils.Thread(target=lambda: (repce.service_loop(),
                                              syncdutils.finalize()))
        t.start()
        logging.info('Agent listining...')

        select((), (), ())


def agent(obj, fd_tup):
    return ChangelogAgent(obj, fd_tup)
