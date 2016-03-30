#!/usr/bin/env python
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

"""
    Description: Library for gluster peer operations.
"""


import re
import time
from distaf.util import tc


def peer_probe(pnode='', servers='', timeout=10):
    """
        Does peer probe and validates the same
        Returns True on success and False on failure
        Note: Input for parameter 'servers' should be in list format
    """
    if pnode == '':
        pnode = tc.nodes[0]
    if servers == '':
        servers = tc.nodes[1:]

    nodes_pool_list = nodes_from_pool_list(pnode)
    if not nodes_pool_list:
        return False
    for server in servers:
        if server not in nodes_pool_list:
            ret = tc.run(pnode, "gluster peer probe %s" % server)
            if ret[0] != 0 or \
                    re.search(r'^peer\sprobe\:\ssuccess(.*)', ret[1]) is None:
                tc.logger.error("Failed to do peer probe for node %s" % server)
                return False
    time.sleep(timeout)

    #Validating whether peer probe is successful
    if not validate_peer_status(pnode, servers):
        tc.logger.error("peer probe validation failed")
        return False

    return True


def peer_detach(pnode='', servers='', force=False, timeout=10):
    """
        Does peer detach and validates the same
        Returns True on success and False on failure
        Note: Input for parameter 'servers' should be in list format
    """
    if pnode == '':
        pnode = tc.nodes[0]
    if servers == '':
        servers = tc.nodes[1:]

    for server in servers:
        if force:
            cmd = "gluster peer detach %s force" % server
        else:
            cmd = "gluster peer detach %s" % server
        ret = tc.run(pnode, cmd)
        if ret[0] != 0 or re.search(r'^peer\sdetach\:\ssuccess(.*)', ret[1]) \
                is None:
            tc.logger.error("Failed to do peer detach for node %s" % server)
            return False

    time.sleep(timeout)
    #Validating whether peer detach is successful
    if validate_peer_status(pnode, servers):
        tc.logger.error("peer detach validatiom failed")
        return False

    return True


def peer_status(pnode=''):
    """
        Does peer status on the given node
        Returns: On success, peer status information in list of dicts
                 If there is only one peer, then an empty list is sent
                 On failure, None
    """
    if pnode == '':
        pnode = tc.nodes[0]
    ret = tc.run(pnode, "gluster peer status")
    if ret[0] != 0:
        tc.logger.error("Failed to execute peer status command in node %s" \
                % pnode)
        return None

    status_list = ret[1].strip().split('\n')
    if "Number of Peers: 0" == status_list[0] and len(status_list) == 1:
        tc.logger.debug("Only one server in the cluster")
        return status_list[1:]

    status_list = status_list[1:]
    peer_list = []
    for status in status_list:
        stat = [stat for stat in status.split('\n') if stat != '']
        temp_dict = {}
        for element in stat:
            elmt = element.split(':')
            temp_dict[elmt[0].strip()] = elmt[1].strip()
        peer_list.append(temp_dict)
    return peer_list


def validate_peer_status(pnode='', servers=''):
    """
        Checks whether peer probe succeeds using peer status command
        Returns True on success and False on failure
        Note: Input for parameter 'servers' should be in list format
    """
    if pnode == '':
        pnode = tc.nodes[0]
    if servers == '':
        servers = tc.nodes[1:]

    check_flag = False
    status = peer_status(pnode)
    if status == []:
        return True
    if status == None:
        return False

    for stat in status:
        if stat['Hostname'] in servers:
            if re.match(r'([0-9a-f]{8})(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', \
                    stat['Uuid'], re.I) is None or \
                    stat['State'] != "Peer in Cluster (Connected)":
                tc.logger.error("peer probe unsuccessful for node %s" % \
                        stat['Hostname'])
                check_flag = True

    if check_flag or not len(set(servers).intersection([stat_key['Hostname'] \
            for stat_key in status])):
        return False

    return True


def pool_list(pnode=''):
    """
        Does pool list on the given node
        Returns: On success, pool list information in list of dictionary format
                 On Failure, None
    """
    if pnode == '':
        pnode = tc.nodes[0]

    ret = tc.run(pnode, "gluster pool list")
    if ret[0] != 0:
        tc.logger.error("Failed to execute pool list in node %s" % pnode)
        return None

    pool_info = []
    for index, item in enumerate(ret[1].split('\n')[:-1]):
        match = re.search(r'(\S+)\s*\t*\s*(\S+)\s*\t*\s*(\S+)\s*', item)
        if match is not None:
            if index == 0:
                keys = match.groups()
            else:
                temp_dict = {}
                for num, element in enumerate(match.groups()):
                    temp_dict[keys[num]] = element
                pool_info.append(temp_dict)

    return pool_info


def nodes_from_pool_list(pnode=''):
    """
        Runs the "gluster pool list" on the specified server
        and returns a list of servers from the pool list

        Returns False on failure and list of servers on success
    """
    if pnode == '':
        pnode = tc.nodes[0]
    servers = []
    nodes_from_pool = pool_list(pnode)
    if not nodes_from_pool:
        return False
    for server in nodes_from_pool:
        if server.get('Hostname') == "localhost":
            servers.insert(0, pnode)
        else:
            servers.append(server.get("Hostname"))
    return servers
