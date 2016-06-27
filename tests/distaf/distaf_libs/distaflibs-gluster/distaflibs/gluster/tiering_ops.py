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
    Description: Library for gluster tiering operations.
"""

import re
from distaf.util import tc
import distaflibs.gluster.volume_ops
from distaflibs.gluster.peer_ops import peer_probe_servers
from distaflibs.gluster.gluster_init import start_glusterd
from distaflibs.gluster.lib_utils import list_files

try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree


def add_peer_nodes_to_cluster(peers, mnode=None):
    """Adds the given peer nodes to cluster

    Args:
        peers (list) : list of peer nodes to be attached to cluster

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        bool: True, if peer nodes are attached to cluster
              False, otherwise

    Example:
        add_peer_nodes_to_cluster(['peer_node1','peer_node2'])
    """

    if mnode is None:
        mnode = tc.servers[0]

    if not isinstance(peers, list):
        peers = [peers]

    ret = start_glusterd(servers=peers)
    if not ret:
        tc.logger.error("glusterd did not start in peer nodes")
        return False

    ret = peer_probe_servers(servers=peers, mnode=mnode)
    if not ret:
        tc.logger.error("Unable to do peer probe on peer machines")
        return False

    return True


def tier_attach(volname, num_bricks_to_add, peers, replica=1, force=False,
                mnode=None):
    """Attaches tier to the volume

    Args:
        volname (str): volume name
        num_bricks_to_add (str): number of bricks to be added as hot tier
        peers (list): from these servers, hot tier will be added to volume

    Kwargs:
        replica (str): replica count of the hot tier
        force (bool): If this option is set to True, then attach tier
            will get executed with force option. If it is set to False,
            then attach tier will get executed without force option
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_attach(testvol, '2', ['peer_node1','peer_node2'])
    """

    if mnode is None:
        mnode = tc.servers[0]

    replica = int(replica)
    repc = ''
    if replica != 1:
        repc = "replica %d" % replica

    frce = ''
    if force:
        frce = 'force'

    num_bricks_to_add = int(num_bricks_to_add)

    from distaflibs.gluster.lib_utils import form_bricks_path
    bricks_path = form_bricks_path(num_bricks_to_add, peers[:],
                                   mnode, volname)
    if bricks_path is None:
        tc.logger.error("number of bricks required are greater than "
                        "unused bricks")
        return (-1, '', '')

    bricks_path = [re.sub(r"(.*\/\S+\_)brick(\d+)", r"\1tier\2", item)
                   for item in bricks_path.split() if item]
    tier_bricks_path = " ".join(bricks_path)
    cmd = ("gluster volume tier %s attach %s %s %s --mode=script"
           % (volname, repc, tier_bricks_path, frce))

    return tc.run(mnode, cmd)


def tier_start(volname, force=False, mnode=None):
    """Starts the tier volume

    Args:
        volname (str): volume name

    Kwargs:
        force (bool): If this option is set to True, then attach tier
            will get executed with force option. If it is set to False,
            then attach tier will get executed without force option
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_start(testvol)
    """

    if mnode is None:
        mnode = tc.servers[0]

    frce = ''
    if force:
        frce = 'force'

    cmd = ("gluster volume tier %s start %s --mode=script"
           % (volname, frce))
    return tc.run(mnode, cmd)


def tier_status(volname, mnode=None):
    """executes tier status command

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_status(testvol)
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s status" % volname
    ret = tc.run(mnode, cmd)

    return ret


def get_tier_status(volname, mnode=None):
    """Parse the output of 'gluster tier status' command.

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success.

    Examples:
        >>> get_tier_status(mnode = 'abc.lab.eng.xyz.com')
        {'node': [{'promotedFiles': '0', 'demotedFiles': '0', 'nodeName':
        'localhost', 'statusStr': 'in progress'}, {'promotedFiles': '0',
        'demotedFiles': '0', 'nodeName': '10.70.47.16', 'statusStr':
        'in progress'}], 'task-id': '2ed28cbd-4246-493a-87b8-1fdcce313b34',
        'nodeCount': '4', 'op': '7'}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s status --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'tier status' on node %s. "
                        "Hence failed to get tier status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster tier status xml output.")
        return None

    tier_status = {}
    tier_status["node"] = []
    for info in root.findall("volRebalance"):
        for element in info.getchildren():
            if element.tag == "node":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                tier_status[element.tag].append(status_info)
            else:
                tier_status[element.tag] = element.text
    return tier_status


def tier_detach_start(volname, mnode=None):
    """starts detaching tier on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_detach_start(testvol)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach start --mode=script" % volname
    return tc.run(mnode, cmd)


def tier_detach_status(volname, mnode=None):
    """executes detach tier status on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_detach_status(testvol)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach status --mode=script" % volname
    return tc.run(mnode, cmd)


def tier_detach_stop(volname, mnode=None):
    """stops detaching tier on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_detach_stop(testvol)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach stop --mode=script" % volname
    return tc.run(mnode, cmd)


def tier_detach_commit(volname, mnode=None):
    """commits detach tier on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_detach_commit(testvol)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach commit --mode=script" % volname
    return tc.run(mnode, cmd)


def tier_detach_force(volname, mnode=None):
    """detaches tier forcefully on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        tier_detach_force(testvol)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach force --mode=script" % volname
    return tc.run(mnode, cmd)


def get_detach_tier_status(volname, mnode=None):
    """Parse the output of 'gluster volume tier detach status' command.

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success.

    Examples:
        >>> get_detach_tier_status("testvol", mnode = 'abc.lab.eng.xyz.com')
        {'node': [{'files': '0', 'status': '3', 'lookups': '1', 'skipped': '0',
        'nodeName': 'localhost', 'failures': '0', 'runtime': '0.00', 'id':
        '11336017-9561-4e88-9ac3-a94d4b403340', 'statusStr': 'completed',
        'size': '0'}, {'files': '0', 'status': '3', 'lookups': '0', 'skipped':
        '0', 'nodeName': '10.70.47.16', 'failures': '0', 'runtime': '0.00',
        'id': 'a2b88b10-eba2-4f97-add2-8dc37df08b27', 'statusStr': 'completed',
        'size': '0'}], 'nodeCount': '4', 'aggregate': {'files': '0', 'status':
        '3', 'lookups': '1', 'skipped': '0', 'failures': '0', 'runtime': '0.0',
        'statusStr': 'completed', 'size': '0'}}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach status --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'detach tier status' on node %s. "
                        "Hence failed to get detach tier status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the detach tier status xml output.")
        return None

    tier_status = {}
    tier_status["node"] = []
    for info in root.findall("volDetachTier"):
        for element in info.getchildren():
            if element.tag == "node":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                tier_status[element.tag].append(status_info)
            elif element.tag == "aggregate":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                tier_status[element.tag] = status_info
            else:
                tier_status[element.tag] = element.text
    return tier_status


def tier_detach_start_and_get_taskid(volname, mnode=None):
    """Parse the output of 'gluster volume tier detach start' command.

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success.

    Examples:
        >>> tier_detach_start_and_get_taskid("testvol",
                                             mnode = 'abc.lab.eng.xyz.com')
        {'task-id': '8020835c-ff0d-4ea1-9f07-62dd067e92d4'}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach start --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'detach tier start' on node %s. "
                        "Hence failed to parse the detach tier start.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster detach tier "
                        "start xml output.")
        return None

    tier_status = {}
    for info in root.findall("volDetachTier"):
        for element in info.getchildren():
            tier_status[element.tag] = element.text
    return tier_status


def tier_detach_stop_and_get_status(volname, mnode=None):
    """Parse the output of 'gluster volume tier detach stop' command.

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success.

    Examples:
        >>> tier_detach_stop_and_get_status("testvol",
                                            mnode = 'abc.lab.eng.xyz.com')
        {'node': [{'files': '0', 'status': '3', 'lookups': '1', 'skipped': '0',
        'nodeName': 'localhost', 'failures': '0', 'runtime': '0.00', 'id':
        '11336017-9561-4e88-9ac3-a94d4b403340', 'statusStr': 'completed',
        'size': '0'}, {'files': '0', 'status': '3', 'lookups': '0', 'skipped':
        '0', 'nodeName': '10.70.47.16', 'failures': '0', 'runtime': '0.00',
        'id': 'a2b88b12-eba2-4f97-add2-8dc37df08b27', 'statusStr': 'completed',
        'size': '0'}], 'nodeCount': '4', 'aggregate': {'files': '0', 'status':
        '3', 'lookups': '1', 'skipped': '0', 'failures': '0', 'runtime': '0.0',
        'statusStr': 'completed', 'size': '0'}}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume tier %s detach stop --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'tier start' on node %s. "
                        "Hence failed to parse the tier start.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster detach tier stop"
                        " xml output.")
        return None

    tier_status = {}
    tier_status["node"] = []
    for info in root.findall("volDetachTier"):
        for element in info.getchildren():
            if element.tag == "node":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                tier_status[element.tag].append(status_info)
            elif element.tag == "aggregate":
                status_info = {}
                for elmt in element.getchildren():
                    status_info[elmt.tag] = elmt.text
                tier_status[element.tag] = status_info
            else:
                tier_status[element.tag] = element.text
    return tier_status


def wait_for_detach_tier_to_complete(volname, mnode=None, timeout=300):
    """Waits for the detach tier to complete

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].
        timeout (int): timeout value to wait for detach tier to complete

    Returns:
        True on success, False otherwise

    Examples:
        >>> wait_for_detach_tier_to_complete("testvol")
    """

    if mnode is None:
        mnode = tc.servers[0]

    count = 0
    flag = 0
    while (count < timeout):
        status_info = get_detach_tier_status(volname, mnode=mnode)
        if status_info is None:
            return False

        status = status_info['aggregate']['statusStr']
        if status == 'completed':
            flag = 1
            break

        time.sleep(10)
        count = count + 10
    if not flag:
        tc.logger.error("detach tier is not completed")
        return False
    else:
        tc.logger.info("detach tier is successfully completed")
    return True


def get_files_from_hot_tier(volname):
    """Lists files from hot tier for the given volume

    Args:
        volname (str): volume name

    Returns:
        Emptylist: if there are no files in hot tier.
        list: list of files in hot tier on success.

    Examples:
        >>>get_files_from_hot_tier("testvol")
    """

    files = []
    subvols = distaflibs.gluster.volume_ops.get_subvols(volname)
    for subvol in subvols['hot_tier_subvols']:
        info = subvol[0].split(':')
        file_list = list_files(info[1], server=info[0])
        for file in file_list:
            if ".glusterfs" not in file:
                files.append(file)

    return files


def get_files_from_cold_tier(volname):
    """Lists files from cold tier for the given volume

    Args:
        volname (str): volume name

    Returns:
        Emptylist: if there are no files in cold tier.
        list: list of files in cold tier on success.

    Examples:
        >>>get_files_from_hot_tier("testvol")
    """

    files = []
    subvols = distaflibs.gluster.volume_ops.get_subvols(volname)
    for subvol in subvols['cold_tier_subvols']:
        info = subvol[0].split(':')
        file_list = list_files(info[1], server=info[0])
        for file in file_list:
            if ".glusterfs" not in file:
                files.append(file)

    return files


def get_tier_promote_frequency(volname):
    """Gets tier promote frequency value for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: promote frequency value on success.

    Examples:
        >>>get_tier_promote_frequency("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.tier-promote-frequency']


def get_tier_demote_frequency(volname):
    """Gets tier demote frequency value for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: demote frequency value on success.

    Examples:
        >>>get_tier_demote_frequency("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.tier-demote-frequency']


def get_tier_mode(volname):
    """Gets tier mode for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: tier mode on success.

    Examples:
        >>>get_tier_mode("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.tier-mode']


def get_tier_max_mb(volname):
    """Gets tier max mb for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: tier max mb on success.

    Examples:
        >>>get_tier_max_mb("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.tier-max-mb']


def get_tier_max_files(volname):
    """Gets tier max files for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: tier max files on success.

    Examples:
        >>>get_tier_max_files("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.tier-max-files']


def get_tier_watermark_high_limit(volname):
    """Gets tier watermark high limit for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: tier watermark high limit on success.

    Examples:
        >>>get_tier_watermark_high_limit("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.watermark-hi']


def get_tier_watermark_low_limit(volname):
    """Gets tier watermark low limit for given volume.

    Args:
        volname (str): volume name

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: tier watermark low limit on success.

    Examples:
        >>>get_tier_watermark_low_limit("testvol")
    """

    vol_options = distaflibs.gluster.volume_ops.get_volume_option(volname)
    if vol_options is None:
        tc.logger.error("Failed to get volume options")
        return None

    return vol_options['cluster.watermark-low']


def set_tier_promote_frequency(volname, value):
    """Sets tier promote frequency value for given volume.

    Args:
        volname (str): volume name
        value (str): promote frequency value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_promote_frequency("testvol", '1000')
    """

    option = {'cluster.tier-promote-frequency': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set promote frequency to %s"
                        % value)
        return False

    return True


def set_tier_demote_frequency(volname, value):
    """Sets tier demote frequency value for given volume.

    Args:
        volname (str): volume name
        value (str): demote frequency value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_demote_frequency("testvol", "500")
    """

    option = {'cluster.tier-demote-frequency': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set demote frequency to %s"
                        % value)
        return False

    return True


def set_tier_mode(volname, value):
    """Sets tier mode for given volume.

    Args:
        volname (str): volume name
        value (str): tier mode value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_mode("testvol", "cache")
    """

    option = {'cluster.tier-mode': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set tier mode to %s"
                        % value)
        return False

    return True


def set_tier_max_mb(volname, value):
    """Sets tier max mb for given volume.

    Args:
        volname (str): volume name
        value (str): tier mode value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_max_mb("testvol", "50")
    """

    option = {'cluster.tier-max-mb': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set tier max mb to %s"
                        % value)
        return False

    return True


def set_tier_max_files(volname, value):
    """Sets tier max files for given volume.

    Args:
        volname (str): volume name
        value (str): tier mode value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_max_files("testvol", "10")
    """

    option = {'cluster.tier-max-files': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set tier max files to %s"
                        % value)
        return False

    return True


def set_tier_watermark_high_limit(volname, value):
    """Sets tier watermark high limit for given volume.

    Args:
        volname (str): volume name
        value (str): tier mode value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_watermark_high_limit("testvol", "95")
    """

    option = {'cluster.watermark-hi': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set tier watermark high limit to %s"
                        % value)
        return False

    return True


def set_tier_watermark_low_limit(volname, value):
    """Sets tier watermark low limit for given volume.

    Args:
        volname (str): volume name
        value (str): tier mode value

    Returns:
        bool: True on success, False Otherwise

    Examples:
        >>>set_tier_watermark_low_limit("testvol", "40")
    """

    option = {'cluster.watermark-low': value}

    if not distaflibs.gluster.volume_ops.set_volume_option(volname,
                                                           options=option):
        tc.logger.error("Failed to set tier watermark low limit to %s"
                        % value)
        return False

    return True


def get_tier_pid(volname, mnode):
    """Gets tier pid for given volume.

    Args:
        volname (str): volume name
        mnode (str): Node on which command has to be executed.

    Returns:
        NoneType: None if command execution fails, parse errors.
        str: pid of tier process on success.

    Examples:
        >>>get_tier_pid("testvol", "abc.xyz.com")
    """

    cmd = ("ps -ef | grep -v grep | grep '/var/log/glusterfs/%s-tier.log' |"
           "awk '{print $2}'" % volname)
    ret, out, err = tc.run(mnode, cmd)
    if ret != 0:
        tc.logger.error("Failed to execute 'ps' cmd")
        return None

    return out.strip("\n")


def is_tier_process_running(volname, mnode):
    """Checks whether tier process is running

    Args:
        volname (str): volume name
        mnode (str): Node on which command has to be executed.

    Returns:
        True on success, False otherwise

    Examples:
        >>>is_tier_process_running("testvol", "abc.xyz.com")
    """

    pid = get_tier_pid(volname, mnode)
    if pid == '':
        return False
    return True
