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


from distaf.util import tc
import re
import time
import socket
try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree


def peer_probe(server, mnode=None):
    """Probe the specified server.

    Args:
        server (str): Server to be peer probed.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster peer probe %s" % server
    return tc.run(mnode, cmd)

def peer_detach(server, force=False, mnode=None):
    """Detach the specified server.

    Args:
        server (str): Server to be peer detached.

    Kwargs:
        force (bool): option to detach peer.
            Defaults to False.
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.
    """
    if mnode is None:
        mnode = tc.servers[0]

    if force:
        cmd = "gluster peer detach %s force" % server
    else:
        cmd = "gluster peer detach %s" % server
    return tc.run(mnode, cmd)


def peer_status(mnode=None):
    """Runs 'gluster peer status' on specified node.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.
    """
    tc.logger.info("Inside peer status")
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster peer status"
    return tc.run(mnode, cmd)


def pool_list(mnode=None):
    """Runs 'gluster pool list' command on the specified node.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster pool list"
    return tc.run(mnode, cmd)


def peer_probe_servers(servers=None, validate=True, time_delay=10, mnode=None):
    """Probe specified servers and validate whether probed servers
    are in cluster and connected state if validate is set to True.

    Kwargs:
        servers (list): List of servers to be peer probed.
            If None, defaults to nodes.
        validate (bool): True to validate if probed peer is in cluster and
            connected state. False otherwise. Defaults to True.
        time_delay (int): time delay before validating peer status.
            Defaults to 10 seconds.
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        bool: True on success and False on failure.
    """
    if servers is None:
        servers = tc.servers[:]
    if not isinstance(servers, list):
        servers = [servers]
    if mnode is None:
        mnode = tc.servers[0]
    if mnode in servers:
        servers.remove(mnode)

    # Get list of nodes from 'gluster pool list'
    nodes_in_pool_list = nodes_from_pool_list(mnode)
    if nodes_in_pool_list is None:
        tc.logger.error("Unable to get nodes from gluster pool list. "
                        "Failing peer probe.")
        return False

    for server in servers:
        if server not in nodes_in_pool_list:
            ret, out, _ = peer_probe(server, mnode)
            if (ret != 0 or
                    re.search(r'^peer\sprobe\:\ssuccess(.*)', out) is None):
                tc.logger.error("Failed to peer probe the node '%s'.", server)
                return False
            else:
                tc.logger.info("Successfully peer probed the node '%s'.",
                               server)

    # Validating whether peer is in connected state after peer probe
    if validate:
        time.sleep(time_delay)
        if not is_peer_connected(servers, mnode):
            tc.logger.error("Validation after peer probe failed.")
            return False
        else:
            tc.logger.info("Validation after peer probe is successful.")

    return True

def peer_detach_servers(servers=None, force=False, validate=True,
                        time_delay=10, mnode=None):
    """Detach peers and validate status of peer if validate is set to True.

    Kwargs:
        servers (list): List of servers to be peer detached.
            If None, defaults to nodes.
        force (bool): option to detach peer.
            Defaults to False.
        validate (bool): True if status of the peer needs to be validated,
            False otherwise. Defaults to True.
        time_delay (int): time delay before executing validating peer.
            status. Defaults to 10 seconds.
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        bool: True on success and False on failure.
    """
    if servers is None:
        servers = tc.servers[:]
    if not isinstance(servers, list):
        servers = [servers]
    if mnode is None:
        mnode = tc.servers[0]
    if mnode in servers:
        servers.remove(mnode)

    for server in servers:
        ret, out, _ = peer_detach(server, force, mnode)
        if (ret != 0 or
                re.search(r'^peer\sdetach\:\ssuccess(.*)', out) is None):
            tc.logger.error("Failed to peer detach the node '%s'.", server)
            return False

    # Validating whether peer detach is successful
    if validate:
        time.sleep(time_delay)
        if is_peer_connected(servers, mnode):
            tc.logger.error("Validation after peer detach failed.")
            return False
        else:
            tc.logger.info("Validation after peer detach is successful")

    return True

def nodes_from_pool_list(mnode=None):
    """Return list of nodes from the 'gluster pool list'.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails.
        list: List of nodes in pool on Success, Empty list on failure.
    """
    if mnode is None:
        mnode = tc.servers[0]

    pool_list_data = get_pool_list(mnode)
    if pool_list_data is None:
        tc.logger.error("Unable to get Nodes from the pool list command.")
        return None

    nodes = []
    for item in pool_list_data:
        nodes.append(item['hostname'])
    return nodes


def get_peer_status(mnode=None):
    """Parse the output of command 'gluster peer status'.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            if None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails or parse errors.
        list: list of dicts on success.

    Examples:
        >>> get_peer_status(mnode = 'abc.lab.eng.xyz.com')
        [{'uuid': '77dc299a-32f7-43d8-9977-7345a344c398',
        'hostname': 'ijk.lab.eng.xyz.com',
        'state': '3',
        'hostnames' : ['ijk.lab.eng.xyz.com'],
        'connected': '1',
        'stateStr': 'Peer in Cluster'},

        {'uuid': 'b15b8337-9f8e-4ec3-8bdb-200d6a67ae12',
        'hostname': 'def.lab.eng.xyz.com',
        'state': '3',
        'hostnames': ['def.lab.eng.xyz.com'],
        'connected': '1',
        'stateStr': 'Peer in Cluster'}
        ]
    """
    if mnode is None:
        mnode = tc.servers[0]

    ret, out, _ = tc.run(mnode, "gluster peer status --xml", verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute peer status command on node '%s'. "
                        "Hence failed to parse the peer status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster peer status xml output.")
        return None

    peer_status_list = []
    for peer in root.findall("peerStatus/peer"):
        peer_dict = {}
        for element in peer.getchildren():
            if element.tag == "hostnames":
                hostnames_list = []
                for hostname in element.getchildren():
                    hostnames_list.append(hostname.text)
                element.text = hostnames_list
            peer_dict[element.tag] = element.text
        peer_status_list.append(peer_dict)
    return peer_status_list


def get_pool_list(mnode=None):
    """Parse the output of 'gluster pool list' command.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: list of dicts on success.

    Examples:
        >>> get_pool_list(mnode = 'abc.lab.eng.xyz.com')
        [{'uuid': 'a2b88b10-eba2-4f97-add2-8dc37df08b27',
        'hostname': 'abc.lab.eng.xyz.com',
        'state': '3',
        'connected': '1',
        'stateStr': 'Peer in Cluster'},

        {'uuid': 'b15b8337-9f8e-4ec3-8bdb-200d6a67ae12',
        'hostname': 'def.lab.eng.xyz.com',
        'state': '3',
        'hostnames': ['def.lab.eng.xyz.com'],
        'connected': '1',
        'stateStr': 'Peer in Cluster'}
        ]
    """
    if mnode is None:
        mnode = tc.servers[0]

    ret, out, _ = tc.run(mnode, "gluster pool list --xml", verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'pool list' on node %s. "
                        "Hence failed to parse the pool list.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster pool list xml output.")
        return None

    pool_list_list = []
    for peer in root.findall("peerStatus/peer"):
        peer_dict = {}
        for element in peer.getchildren():
            if element.tag == "hostname" and element.text == 'localhost':
                element.text = mnode
            if element.tag == "hostnames":
                hostnames_list = []
                for hostname in element.getchildren():
                    hostnames_list.append(hostname.text)
                element.text = hostnames_list
            peer_dict[element.tag] = element.text

        pool_list_list.append(peer_dict)
    return pool_list_list


def is_peer_connected(servers=None, mnode=None):
    """Checks whether specified peers are in cluster and 'Connected' state.

    Kwargs:
        servers (list): List of servers to be validated.
            If None, defaults to nodes.
        mnode (str): Node from which peer probe has to be executed.
            If None, defaults to nodes[0].

    Returns
        bool : True on success (peer in cluster and connected), False on
            failure.
    """
    if servers is None:
        servers = tc.servers[:]
    if not isinstance(servers, list):
        servers = [servers]
    if mnode is None:
        mnode = tc.servers[0]
    if mnode in servers:
        servers.remove(mnode)

    peer_connected = True
    peer_status_list = get_peer_status(mnode)
    if peer_status_list is None:
        tc.logger.error("Failed to parse the peer status. Hence failed to "
                        "validate the peer connected state.")
        return False
    if peer_status_list == []:
        tc.logger.error("No peers present in the pool. Servers are not yet "
                        "connected.")
        return False

    server_ips = []
    for server in servers:
        server_ips.append(socket.gethostbyname(server))

    is_connected = True
    for peer_stat in peer_status_list:
        if socket.gethostbyname(peer_stat['hostname']) in servers:
            if (re.match(r'([0-9a-f]{8})(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}',
                         peer_stat['uuid'], re.I) is None):
                tc.logger.error("Invalid UUID for the node '%s'",
                                peer_stat['hostname'])
                is_connected = False
            if (peer_stat['stateStr'] != "Peer in Cluster" or
                    peer_stat['connected'] != '1'):
                tc.logger.error("Peer '%s' not in connected state",
                                peer_stat['Hostname'])
                is_connected = False

    if not is_connected:
        return False

    peer_nodes = [stat_key['hostname'] for stat_key in peer_status_list]
    if not (set(servers).issubset(peer_nodes)):
        tc.logger.error("Servers: '%s' not yet added to the pool.",
                        (list(set(servers).difference(peer_nodes))))
        return False

    tc.logger.info("Servers: '%s' are all 'Peer in Cluster' and 'Connected' "
                   "state.", servers)
    return True
