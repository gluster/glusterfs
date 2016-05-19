#  This file is part of DiSTAF
#  Copyright (C) 2015-2016  Red Hat, Inc. <http://www.redhat.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


import re
from distaf.util import tc

"""
    This file contains the gluster brick operations like
    add-brick, bring_down_brick replace/remove brick
"""


def add_brick(volname, nbricks, replica=1, stripe=1, peers='', mnode=''):
    """
        Does the gluster add-brick. If peer is '', peers from the config
        is taken. And replica/stripe will not be used by default.
        Returns the output of add-brick command, which would be a tuple of
        (retcode, stdout, sstderr) from gluster add-brick command.
    """
    global tc
    if peers == '':
        peers = tc.peers[:]
    if mnode == '':
        mnode = tc.servers[0]
    replica = int(replica)
    stripe = int(stripe)
    volinfo = tc.run(mnode, "gluster volume info | egrep \"^Brick[0-9]+\"", \
            verbose=False)
    if volinfo[0] != 0:
        tc.logger.error("Unable to get volinfo for add-brick")
        return (-1, -1, -1)
    bi = int(re.findall(r"%s_brick([0-9]+)" % volname, volinfo[1])[-1]) + 1
    tempn = 0
    n = 0
    add_bricks = ''
    brick_root = "/bricks"
    for i in range(bi, bi + nbricks):
        sn = len(re.findall(r"%s" % peers[n], volinfo[1])) + tempn
        add_bricks = "%s %s:%s/brick%d/%s_brick%d" % (add_bricks, peers[n], \
                brick_root, sn, volname, i)
        if n < len(peers[:]) - 1:
            n = n + 1
        else:
            n = 0
            tempn = tempn + 1
    repc = strc = ''
    if replica != 1:
        repc = "replica %d" % replica
    if stripe != 1:
        strc = "stripe %d" % stripe
    ret = tc.run(mnode, "gluster volume add-brick %s %s %s %s" % \
            (volname, repc, strc, add_bricks))
    return ret


def bring_down_brick(volname, bindex, node=''):
    """
        Kills the glusterfsd process of the particular brick
        Returns True on success and False on failure
    """
    global tc
    if node == '':
        node = tc.servers[0]
    ret, rnode, _ = tc.run(node, "gluster volume info %s | egrep \"^Brick%d:\""
                                 " | awk '{print $2}' | awk -F : '{print $1}'"
                                 % (volname, bindex))
    if ret != 0:
        return False
    ret, _, _ = tc.run(rnode.rstrip(), \
    "pid=`cat /var/lib/glusterd/vols/%s/run/*%s_brick%d.pid` && kill -15 $pid \
    || kill -9 $pid" % (volname, volname, bindex - 1))
    if ret != 0:
        return False
    else:
        return True
