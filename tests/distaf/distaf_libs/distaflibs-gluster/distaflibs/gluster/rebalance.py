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
import time
from distaf.util import tc

"""
    Libraries containing gluster rebalance operations
"""

def get_rebal_nodes(server):
    '''
    This function finds out the number of rebalance nodes from
    gluster v info command

    Returns the number of nodes participating in rebalance process
    '''
    val = tc.run(server, \
"gluster v info | grep 'Brick[0-9]' | cut -d ':' -f 2 | sed 's/\ //'")
    nlist = val[1].rstrip().split('\n')
    nnodes = list(set(nlist))
    return len(nnodes)

def get_rebal_dict(status):
    '''
    This function returns the rebalance status info in the form of dictionary
    '''
    _list = status.split('\n')
    rebal_dict = {}
    for _item in _list:
        _match = re.search('.*?(\S+)\s+(\d+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\w+\s*\w+)\s+(\S+).*', _item, re.S)
        if _match is not None:
            rebal_dict[_match.group(1)] = [_match.group(2), _match.group(3), \
                    _match.group(4), _match.group(5), _match.group(6), \
                    _match.group(7),_match.group(8)]
    return rebal_dict


def get_rebal_status(volname, server=''):
    '''
    This function gives rebalance status
    Valid states are started/failed/in progress/completed
    if the server pararmeter is empty it takes node info from config file
    '''
    if server == "":
        server = tc.servers[0]
    status = tc.run(server, "gluster v rebalance %s status" %volname)
    if status[0] != 0:
        if "not started" in status[2]:
            tc.logger.error("Rebalance has not started")
            return ("not started", " ")
        else:
            tc.logger.error("error")
            return ("error", " ")
    else:
        rebal_dict = get_rebal_dict(status[1])
        if "failed" in status[1]:
            tc.logger.error("Rebalance status command failed")
            return ("failed", rebal_dict)
        elif "in progress" in status[1]:
            tc.logger.info("Rebalance is in progress")
            return ("in progress", rebal_dict)
        elif "completed" in status[1]:
            counter = status[1].count("completed")
            nnodes = get_rebal_nodes(server)
            if counter == nnodes:
                tc.logger.info("Rebalance is completed")
                return ("completed", rebal_dict)
            else:
                tc.logger.error("Rebalacne has not completed on all nodes")
                return ("invalid status", rebal_dict)

def wait_rebal_complete(volname, time_out = 300, server=''):
    '''
    This function calls rebalance_status_once function and
    waits if the rebalance status is in progress, exists on timeout,
    default timeout is 300sec(5 min)
    '''
    ret = get_rebal_status(volname, server)
    while time_out != 0 and ret[0] == "in progress":
        time_out = time_out - 20
        time.sleep(20)
        ret = get_rebal_status(volname, server)
    return ret


def rebal_start(volname, server=''):
    """
        Simple interface to start the gluster rebalance
        @ pararmeter:
            * volname
            * server - defaults to tc.servers[0]
        @ returns:
            True on success
            False otherwise
    """
    if server == '':
        server = tc.servers[0]
    ret = tc.run(server, "gluster volume rebalance %s start" % volname)
    if ret[0] != 0:
        tc.logger.error("rebalance start %s failed" % volname)
        return False
    else:
        tc.logger.debug("rebalance start %s successful" % volname)
        return True
