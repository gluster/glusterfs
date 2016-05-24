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
    Description: Library for gluster rebalance operations.
"""

from distaf.util import tc
import re
import time

try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree


def rebalance_start(volname, mnode=None, fix_layout=False, force=False):
    """Starts rebalance on the given volume.

    Example:
        rebalance_start(testvol)

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to nodes[0].
        fix_layout (bool) : If this option is set to True, then rebalance
            start will get execute with fix-layout option. If set to False,
            then rebalance start will get executed without fix-layout option
        force (bool): If this option is set to True, then rebalance
            start will get execute with force option. If it is set to False,
            then rebalance start will get executed without force option

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

    flayout = ''
    if fix_layout:
        flayout = "fix-layout"

    frce = ''
    if force:
        frce = 'force'

    if fix_layout and force:
        tc.logger.warning("Both fix-layout and force option is specified."
                          "Ignoring force option")
        frce = ''

    cmd = "gluster volume rebalance %s %s start %s" % (volname, flayout, frce)
    ret = tc.run(mnode, cmd)
    return ret


def rebalance_stop(volname, mnode=None):
    """Stops rebalance on the given volume.

    Example:
        rebalance_stop(testvol)

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
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

    cmd = "gluster volume rebalance %s stop" % volname
    ret = tc.run(mnode, cmd)
    return ret


def rebalance_status(volname, mnode=None):
    """Executes rebalance status on the given volume.

    Example:
        rebalance_status(testvol)

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
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

    cmd = "gluster volume rebalance %s status" % volname
    ret = tc.run(mnode, cmd)
    return ret


def get_rebalance_status(volname, mnode=None):
    """Parse the output of 'gluster vol rebalance status' command
       for the given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success. rebalance status will be
            in dict format

    Examples:
        >>> get_rebalance_status(testvol, mnode = 'abc.lab.eng.xyz.com')
        {'node': [{'files': '0', 'status': '3', 'lookups': '0', 'skipped': '0',
        'nodeName': 'localhost', 'failures': '0', 'runtime': '0.00', 'id':
        '11336017-9561-4e88-9ac3-a94d4b403340', 'statusStr': 'completed',
        'size': '0'}, {'files': '0', 'status': '1', 'lookups': '0', 'skipped':
        '0', 'nodeName': '10.70.47.16', 'failures': '0', 'runtime': '0.00',
        'id': 'a2b88b10-eba2-4f97-add2-8dc37df08b27', 'statusStr':
        'in progress', 'size': '0'}, {'files': '0', 'status': '3',
        'lookups': '0', 'skipped': '0', 'nodeName': '10.70.47.152',
        'failures': '0', 'runtime': '0.00', 'id':
        'b15b8337-9f8e-4ec3-8bdb-200d6a67ae12', 'statusStr': 'completed',
        'size': '0'}, {'files': '0', 'status': '3', 'lookups': '0', 'skipped':
        '0', 'nodeName': '10.70.46.52', 'failures': '0', 'runtime': '0.00',
        'id': '77dc299a-32f7-43d8-9977-7345a344c398', 'statusStr': 'completed',
        'size': '0'}], 'task-id': 'a16f99d1-e165-40e7-9960-30508506529b',
        'aggregate': {'files': '0', 'status': '1', 'lookups': '0', 'skipped':
        '0', 'failures': '0', 'runtime': '0.00', 'statusStr': 'in progress',
        'size': '0'}, 'nodeCount': '4', 'op': '3'}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume rebalance %s status --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'rebalance status' on node %s. "
                        "Hence failed to get the rebalance status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster rebalance status "
                        "xml output.")
        return None

    rebal_status = {}
    rebal_status["node"] = []
    for info in root.findall("volRebalance"):
        for element in info.getchildren():
            if element.tag == "node":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                rebal_status[element.tag].append(status_info)
            elif element.tag == "aggregate":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                rebal_status[element.tag] = status_info
            else:
                rebal_status[element.tag] = element.text
    return rebal_status


def rebalance_stop_and_get_status(volname, mnode=None):
    """Parse the output of 'gluster vol rebalance stop' command
       for the given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success. rebalance status will be
            in dict format

    Examples:
        >>> rebalance_stop_and_get_status(testvol, mnode = 'abc.xyz.com')
        {'node': [{'files': '0', 'status': '3', 'lookups': '0', 'skipped': '0',
        'nodeName': 'localhost', 'failures': '0', 'runtime': '0.00', 'id':
        '11336017-9561-4e88-9ac3-a94d4b403340', 'statusStr': 'completed',
        'size': '0'}, {'files': '0', 'status': '1', 'lookups': '0', 'skipped':
        '0', 'nodeName': '10.70.47.16', 'failures': '0', 'runtime': '0.00',
        'id': 'a2b88b10-eba2-4f97-add2-8dc37df08b27', 'statusStr':
        'in progress', 'size': '0'}, {'files': '0', 'status': '3',
        'lookups': '0', 'skipped': '0', 'nodeName': '10.70.47.152',
        'failures': '0', 'runtime': '0.00', 'id':
        'b15b8337-9f8e-4ec3-8bdb-200d6a67ae12', 'statusStr': 'completed',
        'size': '0'}, {'files': '0', 'status': '3', 'lookups': '0', 'skipped':
        '0', 'nodeName': '10.70.46.52', 'failures': '0', 'runtime': '0.00',
        'id': '77dc299a-32f7-43d8-9977-7345a344c398', 'statusStr': 'completed',
        'size': '0'}], 'task-id': 'a16f99d1-e165-40e7-9960-30508506529b',
        'aggregate': {'files': '0', 'status': '1', 'lookups': '0', 'skipped':
        '0', 'failures': '0', 'runtime': '0.00', 'statusStr': 'in progress',
        'size': '0'}, 'nodeCount': '4', 'op': '3'}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume rebalance %s stop --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'rebalance stop' on node %s. "
                        "Hence failed to parse the rebalance status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse gluster rebalance stop xml output.")
        return None

    rebal_status = {}
    rebal_status["node"] = []
    for info in root.findall("volRebalance"):
        for element in info.getchildren():
            if element.tag == "node":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                rebal_status[element.tag].append(status_info)
            elif element.tag == "aggregate":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                rebal_status[element.tag] = status_info
            else:
                rebal_status[element.tag] = element.text
    return rebal_status


def wait_for_rebalance_to_complete(volname, mnode=None, timeout=300):
    """Waits for the rebalance to complete

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].
        timeout (int): timeout value in seconds to wait for rebalance
            to complete

    Returns:
        True on success, False otherwise

    Examples:
        >>> wait_for_rebalance_to_complete("testvol")
    """

    if mnode is None:
        mnode = tc.servers[0]

    count = 0
    flag = 0
    while (count < timeout):
        status_info = get_rebalance_status(volname, mnode=mnode)
        if status_info is None:
            return False

        status = status_info['aggregate']['statusStr']
        if status == 'completed':
            flag = 1
            break

        time.sleep(10)
        count = count + 10
    if not flag:
        tc.logger.error("rebalance is not completed")
        return False
    else:
        tc.logger.info("rebalance is successfully completed")
    return True
