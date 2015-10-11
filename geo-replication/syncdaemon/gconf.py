#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#


class GConf(object):

    """singleton class to store globals
       shared between gsyncd modules"""

    ssh_ctl_dir = None
    ssh_ctl_args = None
    cpid = None
    pid_file_owned = False
    log_exit = False
    permanent_handles = []
    log_metadata = {}
    """One variable is sufficient to track the
       switching of worker to ACTIVE. Two variables
       are intentionally used to track worker going
       to PASSIVE as well mainly for debugging"""
    active_earlier = False
    passive_earlier = False
    mgmt_lock_fd = None

gconf = GConf()
