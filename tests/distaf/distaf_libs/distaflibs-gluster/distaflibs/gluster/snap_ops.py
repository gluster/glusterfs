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
    Description: Library for gluster snapshot operations.
"""

from distaf.util import tc
from distaflibs.gluster.volume_ops import start_volume, stop_volume
import re
import time

try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree


def snap_create(volname, snapname, timestamp=False, description='',
                force=False, mnode=None):
    """Creates snapshot for the given volume.

    Example:
        snap_create(testvol, testsnap)

    Args:
        volname (str): volume name
        snapname (str): snapshot name

    Kwargs:
        timestamp (bool): If this option is set to True, then
            timestamps will get appended to the snapname. If this option
            is set to False, then timestamps will not be appended to snapname.
        description (str): description for snapshot creation
        force (bool): If this option is set to True, then snap
            create will get execute with force option. If it is set to False,
            then snap create will get executed without force option
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

    if description != '':
        description = "description '%s'" % description

    tstamp = ''
    if not timestamp:
        tstamp = "no-timestamp"

    frce = ''
    if force:
        frce = 'force'

    cmd = ("gluster snapshot create %s %s %s %s %s"
           % (snapname, volname, tstamp, description, frce))
    ret = tc.run(mnode, cmd)
    return ret


def snap_clone(snapname, clonename, mnode=None):
    """Clones the given snapshot

    Example:
        snap_clone(testsnap, clone1)

    Args:
        snapname (str): snapshot name to be cloned
        clonename (str): clone name

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
    cmd = "gluster snapshot clone %s %s --mode=script" % (clonename, snapname)
    return tc.run(mnode, cmd)


def snap_restore(snapname, mnode=None):
    """Executes snap restore cli for the given snapshot

    Example:
        snap_restore(testsnap)

    Args:
        snapname (str): snapshot name to be cloned

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
    cmd = "gluster snapshot restore %s --mode=script" % snapname
    return tc.run(mnode, cmd)


def snap_restore_complete(volname, snapname, mnode=None):
    """stops the volume restore the snapshot and starts the volume

    Example:
        snap_restore_complete(testvol, testsnap)

    Args:
        volname (str): volume name
        snapname (str): snapshot name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to nodes[0].

    Returns:
        bool: True on success, False on failure
    """
    if mnode is None:
        mnode = tc.servers[0]

    # Stopping volume before snap restore
    ret = stop_volume(volname, mnode)
    if not ret:
        tc.logger.error("Failed to stop volume %s before restoring snapshot "
                        "%s in node %s" % (volname, snapname, mnode))
        return False
    ret, _, _ = snap_restore(snapname, mnode=mnode)
    if ret != 0:
        tc.logger.error("snapshot restore cli execution failed")
        return False

    # Starting volume after snap restore
    ret = start_volume(volname, mnode)
    if not ret:
        tc.logger.error("Failed to start volume %s after restoring snapshot "
                        "%s in node %s" % (volname, snapname, mnode))
        return False
    return True


def snap_status(snapname="", volname="", mnode=None):
    """Runs 'gluster snapshot status' on specific node

    Example:
        snap_status()

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to nodes[0].
        snapname (str): snapshot name
        volname (str): volume name

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

    if snapname != "" and volname != "":
        tc.logger.error("Incorrect cmd. snap status cli accepts either "
                        "snapname or volname")
        return (-1, None, None)

    if volname != '':
        volname = "volume %s" % volname

    cmd = "gluster snapshot status %s %s" % (snapname, volname)
    return tc.run(mnode, cmd)


def get_snap_status(mnode=None):
    """Parse the output of 'gluster snapshot status' command.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: list of dict on success. Each snap status will be
            in dict format

    Examples:
        >>> get_snap_status(mnode = 'abc.lab.eng.xyz.com')
        [{'volCount': '1', 'volume': {'brick': [{'path': '10.70.47.11:
        testvol_brick0', 'pid': '26747', 'lvUsage': '3.52', 'volumeGroup':
        'RHS_vg0', 'lvSize': '9.95g'}, {'path': '10.70.47.16:/testvol_brick1',
        'pid': '25497', 'lvUsage': '3.52', 'volumeGroup': 'RHS_vg0',
        'lvSize': '9.95g'}], 'brickCount': '2'}, 'name': 'snap2', 'uuid':
        '56a39a92-c339-47cc-a8b2-9e54bb2a6324'}, {'volCount': '1', 'volume':
        {'brick': [{'path': '10.70.47.11:testvol_next_brick0', 'pid': '26719',
        'lvUsage': '4.93', 'volumeGroup': 'RHS_vg1', 'lvSize': '9.95g'}],
        'brickCount': '1'}, 'name': 'next_snap1',
        'uuid': 'dcf0cd31-c0db-47ad-92ec-f72af2d7b385'}]
    """

    if mnode is None:
        mnode = tc.servers[0]

    ret, out, _ = tc.run(mnode, "gluster snapshot status --xml", verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'snapshot status' on node %s. "
                        "Hence failed to get the snapshot status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster snapshot "
                        "status xml output.")
        return None

    snap_status_list = []
    for snap in root.findall("snapStatus/snapshots/snapshot"):
        snap_status = {}
        for element in snap.getchildren():
            if element.tag == "volume":
                status = {}
                status["brick"] = []
                for elmt in element.getchildren():
                    if elmt.tag == "brick":
                        brick_info = {}
                        for el in elmt.getchildren():
                            brick_info[el.tag] = el.text
                        status["brick"].append(brick_info)
                    else:
                        status[elmt.tag] = elmt.text

                snap_status[element.tag] = status
            else:
                snap_status[element.tag] = element.text
        snap_status_list.append(snap_status)
    return snap_status_list


def get_snap_status_by_snapname(snapname, mnode=None):
    """Parse the output of 'gluster snapshot status' command
        for the given snapshot.

    Args:
        snapname (str): snapshot name
    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: on success.

    Examples:
        >>> get_snap_status_by_snapname('snap1',
                                          mnode = 'abc.lab.eng.xyz.com')
        {'volCount': '1', 'volume': {'brick': [{'path': '10.70.47.11:
        testvol_brick0', 'pid': '26747', 'lvUsage': '3.52', 'volumeGroup':
        'RHS_vg0', 'lvSize': '9.95g'}, {'path': '10.70.47.16:/testvol_brick1',
        'pid': '25497', 'lvUsage': '3.52', 'volumeGroup': 'RHS_vg0',
        'lvSize': '9.95g'}], 'brickCount': '2'}, 'name': 'snap2', 'uuid':
        '56a39a92-c339-47cc-a8b2-9e54bb2a6324'}
    """

    if mnode is None:
        mnode = tc.servers[0]

    snap_status_list = get_snap_status(mnode=mnode)
    if not snap_status_list:
        tc.logger.error("Failed to parse snap status in "
                        "get_snap_status_by_snapname()")
        return None

    for snap_status in snap_status_list:
        if "name" in snap_status:
            if snap_status["name"] == snapname:
                return snap_status
    tc.logger.error("The snap %s not found" % (snapname))
    return None


def get_snap_status_by_volname(volname, mnode=None):
    """Parse the output of 'gluster snapshot status' command
        for the given volume.

    Args:
        volname (str): snapshot name
    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: list of dicts on success.

    Examples:
        >>> get_snap_status_by_volname('testvol',
                                         mnode = 'abc.lab.eng.xyz.com')
        [{'volCount': '1', 'volume': {'brick': [{'path': '10.70.47.11:
        testvol_brick0', 'pid': '26747', 'lvUsage': '3.52', 'volumeGroup':
        'RHS_vg0', 'lvSize': '9.95g'}, {'path': '10.70.47.16:/testvol_brick1',
        'pid': '25497', 'lvUsage': '3.52', 'volumeGroup': 'RHS_vg0',
        'lvSize': '9.95g'}], 'brickCount': '2'}, 'name': 'snap2', 'uuid':
        '56a39a92-c339-47cc-a8b2-9e54bb2a6324'}, {'volCount': '1', 'volume':
        {'brick': [{'path': '10.70.47.11:testvol_next_brick0', 'pid': '26719',
        'lvUsage': '4.93', 'volumeGroup': 'RHS_vg1', 'lvSize': '9.95g'}],
        'brickCount': '1'}, 'name': 'next_snap1',
        'uuid': 'dcf0cd31-c0db-47ad-92ec-f72af2d7b385'}]
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster snapshot status volume %s --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'snapshot status' on node %s. "
                        "Hence failed to get the snapshot status.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster snapshot "
                        "status xml output.")
        return None

    snap_status_list = []
    for snap in root.findall("snapStatus/snapshots/snapshot"):
        snap_status = {}
        for element in snap.getchildren():
            if element.tag == "volume":
                status = {}
                status["brick"] = []
                for elmt in element.getchildren():
                    if elmt.tag == "brick":
                        brick_info = {}
                        for el in elmt.getchildren():
                            brick_info[el.tag] = el.text
                        status["brick"].append(brick_info)
                    else:
                        status[elmt.tag] = elmt.text

                snap_status[element.tag] = status
            else:
                snap_status[element.tag] = element.text
        snap_status_list.append(snap_status)
    return snap_status_list


def snap_info(snapname="", volname="", mnode=None):
    """Runs 'gluster snapshot info' on specific node

    Example:
        snap_info()

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to nodes[0].
        snapname (str): snapshot name
        volname (str): volume name

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

    if snapname != "" and volname != "":
        tc.logger.error("Incorrect cmd. snap info cli accepts either "
                        "snapname or volname")
        return (-1, None, None)

    if volname != '':
        volname = "volume %s" % volname

    cmd = "gluster snapshot info %s %s" % (snapname, volname)
    return tc.run(mnode, cmd)


def get_snap_info(mnode=None):
    """Parse the output of 'gluster snapshot info' command.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: list of dicts on success.

    Examples:
        >>> get_snap_info(mnode = 'abc.lab.eng.xyz.com')
        [{'description': 'This is snap2', 'uuid':
        '56a39a92-c339-47cc-a8b2-9e54bb2a6324', 'volCount': '1',
        'snapVolume': {'status': 'Stopped', 'name':
        'df1882d3f86d48738e69f298096f3810'}, 'createTime':
        '2016-04-07 12:01:21', 'name': 'snap2'}, {'description': None,
        'uuid': 'a322d93a-2732-447d-ab88-b943fa402fd2', 'volCount': '1',
        'snapVolume': {'status': 'Stopped', 'name':
        '2c790e6132e447e79168d9708d4abfe7'}, 'createTime':
        '2016-04-07 13:59:43', 'name': 'snap1'}]
    """
    if mnode is None:
        mnode = tc.servers[0]

    ret, out, _ = tc.run(mnode, "gluster snapshot info --xml", verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'snapshot info' on node %s. "
                        "Hence failed to get the snapshot info.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster snapshot "
                        "info xml output.")
        return None

    snap_info_list = []
    for snap in root.findall("snapInfo/snapshots/snapshot"):
        snap_info = {}
        for element in snap.getchildren():
            if element.tag == "snapVolume":
                info = {}
                for elmt in element.getchildren():
                    if elmt.tag == "originVolume":
                        info["originVolume"] = {}
                        for el in elmt.getchildren():
                            info[elmt.tag][el.tag] = el.text
                    else:
                        info[elmt.tag] = elmt.text
                snap_info[element.tag] = info
            else:
                snap_info[element.tag] = element.text
        snap_info_list.append(snap_info)
    return snap_info_list


def get_snap_info_by_snapname(snapname, mnode=None):
    """Parse the output of 'gluster snapshot info' command
        for the given snapshot.

    Args:
        snapname (str): snapshot name
    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: on success.

    Examples:
        >>> get_snap_info_by_snapname('snap1', mnode = 'abc.lab.eng.xyz.com')
        {'description': 'This is snap2', 'uuid':
        '56a39a92-c339-47cc-a8b2-9e54bb2a6324', 'volCount': '1',
        'snapVolume': {'status': 'Stopped', 'name':
        'df1882d3f86d48738e69f298096f3810'}
    """

    if mnode is None:
        mnode = tc.servers[0]

    snap_info_list = get_snap_info(mnode=mnode)
    if not snap_info_list:
        tc.logger.error("Failed to parse snap info in "
                        "get_snap_info_by_snapname()")
        return None

    for snap_info in snap_info_list:
        if "name" in snap_info:
            if snap_info["name"] == snapname:
                return snap_info
    tc.logger.error("The snap %s not found" % (snapname))
    return None


def get_snap_info_by_volname(volname, mnode=None):
    """Parse the output of 'gluster snapshot info' command
        for the given volume.

    Args:
        volname (str): snapshot name
    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: list of dicts on success.

    Examples:
        >>> get_snap_info_by_volname('testvol',
                                       mnode = 'abc.lab.eng.xyz.com')
        {'originVolume': {'snapCount': '1', 'name': 'testvol',
        'snapRemaining': '255'}, 'count': '1', 'snapshots':
        [{'description': 'This is next snap1', 'uuid':
        'dcf0cd31-c0db-47ad-92ec-f72af2d7b385', 'volCount': '1',
        'snapVolume': {'status': 'Stopped', 'name':
        '49c290d6e8b74205adb3cce1206b5bc5'}, 'createTime':
        '2016-04-07 12:03:11', 'name': 'next_snap1'}]}
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster snapshot info volume %s --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'snapshot info' on node %s. "
                        "Hence failed to get the snapshot info.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster snapshot "
                        "info xml output.")
        return None

    snap_vol_info = {}

    for snap in root.findall("snapInfo"):
        for element in snap.getchildren():
            if element.tag == "originVolume":
                info = {}
                for elmt in element.getchildren():
                    info[elmt.tag] = elmt.text
                snap_vol_info[element.tag] = info
            else:
                snap_vol_info[element.tag] = element.text

    snap_info_list = []
    for snap in root.findall("snapInfo/snapshots/snapshot"):
        snap_info = {}
        for element in snap.getchildren():
            if element.tag == "snapVolume":
                info = {}
                for elmt in element.getchildren():
                    if elmt.tag == "originVolume":
                        info["originVolume"] = {}
                        for el in elmt.getchildren():
                            info[elmt.tag][el.tag] = el.text
                    else:
                        info[elmt.tag] = elmt.text
                snap_info[element.tag] = info
            else:
                snap_info[element.tag] = element.text
        snap_info_list.append(snap_info)
    snap_vol_info["snapshots"] = snap_info_list
    return snap_vol_info


def snap_list(mnode=None):
    """Lists the snapshots

    Example:
        snap_list()

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
    cmd = "gluster snapshot list"
    return tc.run(mnode, cmd)


def get_snap_list(mnode=None):
    """Parse the output of 'gluster snapshot list' command.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: list of snapshots on success.

    Examples:
        >>> get_snap_list(mnode = 'abc.lab.eng.xyz.com')
        ['snap1', 'snap2']
    """
    if mnode is None:
        mnode = tc.servers[0]

    ret, out, _ = tc.run(mnode, "gluster snapshot list --xml", verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'snapshot list' on node %s. "
                        "Hence failed to get the snapshot list.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster snapshot "
                        "list xml output.")
        return None

    snap_list = []
    for snap in root.findall("snapList/snapshot"):
        snap_list.append(snap.text)

    return snap_list


def snap_config(volname=None, mnode=None):
    """Runs 'gluster snapshot config' on specific node

    Example:
        snap_config()

    Kwargs:
        volname (str): volume name
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

    if volname is None:
        volname = ""

    cmd = "gluster snapshot config %s" % volname
    return tc.run(mnode, cmd)


def get_snap_config(volname=None, mnode=None):
    """Parse the output of 'gluster snapshot config' command.

    Kwargs:
        volname (str): volume name
        mnode (str): Node on which command has to be executed.
            If None, defaults to nodes[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: on success.

    Examples:
        >>> get_snap_config()
        {'volumeConfig': [{'softLimit': '230', 'effectiveHardLimit': '256',
        'name': 'testvol', 'hardLimit': '256'}, {'softLimit': '230',
        'effectiveHardLimit': '256', 'name': 'testvol_next',
        'hardLimit': '256'}], 'systemConfig': {'softLimit': '90%',
        'activateOnCreate': 'disable', 'hardLimit': '256',
        'autoDelete': 'disable'}}
    """
    if mnode is None:
        mnode = tc.servers[0]

    ret, out, _ = tc.run(mnode, "gluster snapshot config --xml", verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'snapshot config' on node %s. "
                        "Hence failed to get the snapshot config.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster snapshot "
                        "config xml output.")
        return None

    snap_config = {}
    for config in root.findall("snapConfig/systemConfig"):
        sys_config = {}
        for element in config.getchildren():
            sys_config[element.tag] = element.text
    snap_config["systemConfig"] = sys_config

    volume_config = []
    for config in root.findall("snapConfig/volumeConfig/volume"):
        vol_config = {}
        for element in config.getchildren():
            vol_config[element.tag] = element.text

        if volname is not None:
            if volname == vol_config["name"]:
                volume_config.append(vol_config)
        else:
            volume_config.append(vol_config)

    snap_config["volumeConfig"] = volume_config
    return snap_config


def set_snap_config(option, volname=None, mnode=None):
    """Sets given snap config on the given node

    Example:
        >>>option={'snap-max-hard-limit':'200'}
        set_snap_config(option)

    Args:
        option (dict): dict of single snap config option
    Kwargs:
        volname (str): volume name
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

    if volname is None:
        volname = ""

    cmd = ("gluster snapshot config %s %s %s --mode=script"
           % (volname, option.keys()[0], option.values()[0]))
    return tc.run(mnode, cmd)


def snap_delete(snapname, mnode=None):
    """Deletes the given snapshot

    Example:
        snap_delete(testsnap)

    Args:
        snapname (str): snapshot name to be deleted

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

    cmd = "gluster snapshot delete %s --mode=script" % snapname
    return tc.run(mnode, cmd)


def snap_delete_by_volumename(volname, mnode=None):
    """Deletes the given snapshot

    Example:
        snap_delete_by_volumename(testvol)

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

    cmd = "gluster snapshot delete volume %s --mode=script" % volname
    return tc.run(mnode, cmd)


def snap_delete_all(mnode=None):
    """Deletes all the snapshot in the cluster

    Example:
        snap_delete_all(testsnap)

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
    cmd = "gluster snapshot delete all --mode=script"
    return tc.run(mnode, cmd)


def snap_activate(snapname, force=False, mnode=None):
    """Activates the given snapshot

    Example:
        snap_activate(testsnap)

    Args:
        snapname (str): snapshot name to be cloned

    Kwargs:
        force (bool): If this option is set to True, then snap
            activate will get execute with force option. If it is set to False,
            then snap activate will get executed without force option
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

    frce = ''
    if force:
        frce = 'force'

    cmd = "gluster snapshot activate %s %s --mode=script" % (snapname, frce)
    return tc.run(mnode, cmd)


def snap_deactivate(snapname, mnode=None):
    """Deactivates the given snapshot

    Example:
        snap_deactivate(testsnap)

    Args:
        snapname (str): snapshot name to be cloned

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
    cmd = "gluster snapshot deactivate %s --mode=script" % snapname
    return tc.run(mnode, cmd)
