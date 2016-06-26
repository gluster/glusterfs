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


import re
import time
from distaf.util import tc
from pprint import pformat
try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree
from distaflibs.gluster.mount_ops import mount_volume
from distaflibs.gluster.gluster_init import env_setup_servers, start_glusterd
from distaflibs.gluster.peer_ops import (peer_probe_servers,
                                         nodes_from_pool_list)

"""
    This file contains the gluster volume operations like create volume,
    start/stop volume etc
"""


def create_volume(volname, mnode=None, dist=1, rep=1, stripe=1, trans='tcp',
                  servers=None, disp=1, dispd=1, red=1):
    """Create the gluster volume specified configuration
       volname and distribute count are mandatory argument
    Args:
        volname(str): volume name that has to be created

    Kwargs:
        mnode(str): server on which command has to be execeuted,
            defaults to tc.servers[0]
        dist(int): distribute count, defaults to 1
        rep(int): replica count, defaults to 1
        stripe(int): stripe count, defaults to 1
        trans(str): transport type, defaults to tcp
        servers(list): servers on which volume has to be created,
            defaults to number of servers in pool list, if that is None,
            then takes tc.servers
        disp(int): disperse count, defaults to 1
        dispd(int): disperse-data count, defaults to 1
        red(int): rdundancy count, defaults to 1

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

           (-1, '', ''): If not enough bricks are available to create volume.
           (ret, out, err): As returned by volume create command execution.

    Example:
        create_volume(volname)
    """
    if servers is None:
        servers = nodes_from_pool_list()
    if not servers:
        servers = tc.servers[:]
    if mnode is None:
        mnode = tc.servers[0]
    dist = int(dist)
    rep = int(rep)
    stripe = int(stripe)
    disp = int(disp)
    dispd = int(dispd)
    red = int(red)
    dispc = 1

    if disp != 1 and dispd != 1:
        tc.logger.error("volume can't have both disperse and disperse-data")
        return (-1, None, None)
    if disp != 1:
        dispc = int(disp)
    elif dispd != 1:
        dispc = int(dispd) + int(red)

    number_of_bricks = dist * rep * stripe * dispc
    replica = stripec = disperse = disperse_data = redundancy = ''

    from distaflibs.gluster.lib_utils import form_bricks_path
    bricks_path = form_bricks_path(number_of_bricks, servers[:],
                                   mnode, volname)
    if bricks_path is None:
        tc.logger.error("number of bricks required are greater than "
                        "unused bricks")
        return (-1, '', '')

    if rep != 1:
        replica = "replica %d" % rep
    if stripe != 1:
        stripec = "stripe %d" % stripe
    ttype = "transport %s" % trans
    if disp != 1:
        disperse = "disperse %d" % disp
        redundancy = "redundancy %d" % red
    elif dispd != 1:
        disperse_data = "disperse-data %d" % dispd
        redundancy = "redundancy %d" % red

    ret = tc.run(mnode, "gluster volume create %s %s %s %s %s %s %s %s "
                 "--mode=script" % (volname, replica, stripec, disperse,
                                    disperse_data, redundancy, ttype,
                                    bricks_path))

    return ret


def start_volume(volname, mnode=None, force=False):
    """Starts the gluster volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
        force (bool): If this option is set to True, then start volume
            will get executed with force option. If it is set to False,
            then start volume will get executed without force option

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        start_volume("testvol")
    """
    if mnode is None:
        mnode = tc.servers[0]
    frce = ''
    if force:
        frce = 'force'
    cmd = "gluster volume start %s %s --mode=script" % (volname, frce)
    return tc.run(mnode, cmd)


def stop_volume(volname, mnode=None, force=False):
    """Stops the gluster volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
        force (bool): If this option is set to True, then stop volume
            will get executed with force option. If it is set to False,
            then stop volume will get executed without force option

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        stop_volume("testvol")
    """
    if mnode is None:
        mnode = tc.servers[0]
    frce = ''
    if force:
        frce = 'force'
    cmd = "gluster volume stop %s %s --mode=script" % (volname, frce)
    return tc.run(mnode, cmd)


def delete_volume(volname, mnode=None):
    """Deletes the gluster volume if given volume exists in
       gluster and deletes the directories in the bricks
       associated with the given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        bool: True, if volume is deleted
              False, otherwise
    """

    if mnode is None:
        mnode = tc.servers[0]

    volinfo = get_volume_info(volname, mnode)
    if volinfo is None or volname not in volinfo:
        tc.logger.info("Volume %s does not exist in %s" % (volname, mnode))
        return True

    if volinfo[volname]['typeStr'] == 'Tier':
        tmp_hot_brick = volinfo[volname]["bricks"]["hotBricks"]["brick"]
        hot_bricks = [x["name"] for x in tmp_hot_brick if "name" in x]
        tmp_cold_brick = volinfo[volname]["bricks"]["coldBricks"]["brick"]
        cold_bricks = [x["name"] for x in tmp_cold_brick if "name" in x]
        bricks = hot_bricks + cold_bricks
    else:
        bricks = [x["name"] for x in volinfo[volname]["bricks"]["brick"]
                  if "name" in x]
    ret, _, _ = tc.run(mnode, "gluster volume delete %s --mode=script"
                       % volname)
    if ret != 0:
        return False
    try:
        del tc.global_flag[volname]
    except KeyError:
        pass
    for brick in bricks:
        node, vol_dir = brick.split(":")
        ret = tc.run(node, "rm -rf %s" % vol_dir)

    return True


def reset_volume(volname, mnode=None, force=False):
    """Resets the gluster volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
        force (bool): If this option is set to True, then reset volume
            will get executed with force option. If it is set to False,
            then reset volume will get executed without force option

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        reset_volume("testvol")
    """
    if mnode is None:
        mnode = tc.servers[0]
    frce = ''
    if force:
        frce = 'force'
    cmd = "gluster volume reset %s %s --mode=script" % (volname, frce)
    return tc.run(mnode, cmd)


def cleanup_volume(volname, mnode=None):
    """deletes snapshots in the volume, stops and deletes the gluster
       volume if given volume exists in gluster and deletes the
       directories in the bricks associated with the given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        bool: True, if volume is deleted successfully
              False, otherwise

    Example:
        cleanup_volume("testvol")
    """
    from distaflibs.gluster.snap_ops import snap_delete_by_volumename

    if mnode is None:
        mnode = tc.servers[0]

    ret, _, _ = snap_delete_by_volumename(volname, mnode=mnode)
    if ret != 0:
        tc.logger.error("Failed to delete the snapshots in "
                        "volume %s" % volname)
        return False

    ret, _, _ = stop_volume(volname, mnode, True)
    if ret != 0:
        tc.logger.error("Failed to stop volume %s" % volname)
        return False

    ret = delete_volume(volname, mnode)
    if not ret:
        tc.logger.error("Unable to cleanup the volume %s" % volname)
        return False
    return True


def setup_vol(volname, mnode=None, dist=1, rep=1, dispd=1, red=1,
              stripe=1, trans="tcp", servers=None):
    """
        Setup a gluster volume for testing.
        It first formats the back-end bricks and then creates a
        trusted storage pool by doing peer probe. And then it creates
        a volume of specified configuration.

        When the volume is created, it sets a global flag to indicate
        that the volume is created. If another testcase calls this
        function for the second time with same volume name, the function
        checks for the flag and if found, will return True.
    Args:
        volname(str): volume name that has to be created

    Kwargs:
        mnode(str): server on which command has to be execeuted,
            defaults to tc.servers[0]
        dist(int): distribute count, defaults to 1
        rep(int): replica count, defaults to 1
        stripe(int): stripe count, defaults to 1
        trans(str): transport type, defaults to tcp
        servers(list): servers on which volume has to be created,
            defaults to number of servers in pool list, if that is None,
            then takes tc.servers
        disp(int): disperse count, defaults to 1
        dispd(int): disperse-data count, defaults to 1
        red(int): rdundancy count, defaults to 1

    Returns:
        bool: True on success and False for failure.
    """
    if servers is None:
        servers = tc.servers[:]
    if mnode is None:
        mnode = tc.servers[0]
    volinfo = get_volume_info(mnode=mnode)
    if volinfo is not None and volname in volinfo.keys():
        tc.logger.debug("volume %s already exists in %s. Returning..." \
                % (volname, servers[0]))
        return True
    ret = env_setup_servers(servers=servers)
    if not ret:
        tc.logger.error("Formatting backend bricks failed. Aborting...")
        return False
    ret = start_glusterd(servers)
    if not ret:
        tc.logger.error("glusterd did not start in at least one server")
        return False
    time.sleep(5)
    ret = peer_probe_servers(servers[1:], mnode=mnode)
    if not ret:
        tc.logger.error("Unable to peer probe one or more machines")
        return False
    if rep != 1 and dispd != 1:
        tc.logger.warning("Both replica count and disperse count is specified")
        tc.logger.warning("Ignoring the disperse and using the replica count")
        dispd = 1
        red = 1
    ret = create_volume(volname, mnode, dist, rep, stripe, trans, servers,
                        dispd=dispd, red=red)
    if ret[0] != 0:
        tc.logger.error("Unable to create volume %s" % volname)
        return False
    time.sleep(2)
    ret = start_volume(volname, mnode)
    if not ret:
        tc.logger.error("volume start %s failed" % volname)
        return False
    if tc.global_config["gluster"]["cluster_config"]["nfs_ganesha"]["enable"]:
        from distaflibs.gluster.ganesha import vol_set_ganesha
        ret = vol_set_ganesha(volname)
        if not ret:
            tc.logger.error("failed to set the ganesha option for %s" % volname)
            return False
    tc.global_flag[volname] = True
    return True


def volume_status(volname='all', service='', options='', mnode=None):
    """Executes gluster volume status cli command

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
        volname (str): volume name. Defaults to 'all'
        service (str): name of the service to get status.
            serivce can be, [nfs|shd|<BRICK>|quotad]], If not given,
            the function returns all the services
        options (str): options can be,
            [detail|clients|mem|inode|fd|callpool|tasks]. If not given,
            the function returns the output of gluster volume status

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        volume_status()
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster vol status %s %s %s" % (volname, service, options)

    return tc.run(mnode, cmd)


def _parse_volume_status_xml(root_xml):
    """
    Helper module for get_volume_status. It takes root xml object as input,
    parses and returns the 'volume' tag xml object.
    """

    for element in root_xml:
        if element.findall("volume"):
            return element.findall("volume")
        root_vol = _parse_volume_status_xml(element)
        if root_vol is not None:
            return root_vol


def parse_xml(tag_obj):
    """
    This helper module takes any xml element object and parses all the child
    nodes and returns the parsed data in dictionary format
    """
    node_dict = {}
    for tag in tag_obj:
        if re.search(r'\n\s+', tag.text) is not None:
            port_dict = {}
            port_dict = parse_xml(tag)
            node_dict[tag.tag] = port_dict
        else:
            node_dict[tag.tag] = tag.text
    return node_dict


def get_volume_status(volname='all', service='', options='', mnode=None):
    """
    This module gets the status of all or specified volume(s)/brick

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].
        volname (str): volume name. Defaults to 'all'
        service (str): name of the service to get status.
            serivce can be, [nfs|shd|<BRICK>|quotad]], If not given,
            the function returns all the services
        options (str): options can be,
            [detail|clients|mem|inode|fd|callpool|tasks]. If not given,
            the function returns the output of gluster volume status
    Returns:
        dict: volume status in dict of dictionary format, on success
        NoneType: on failure

    Example:
        get_volume_status(volname="testvol")
        >>>{'testvol': {'10.70.47.89': {'/bricks/brick1/a11': {'status': '1',
        'pid': '28963', 'bricktype': 'cold', 'port': '49163', 'peerid':
        '7fc9015e-8134-4753-b837-54cbc6030c98', 'ports': {'rdma': 'N/A',
        'tcp': '49163'}}, '/bricks/brick2/a31': {'status': '1', 'pid':
        '28982', 'bricktype': 'cold', 'port': '49164', 'peerid':
        '7fc9015e-8134-4753-b837-54cbc6030c98', 'ports': {'rdma': 'N/A',
        'tcp': '49164'}}, 'NFS Server': {'status': '1', 'pid': '30525',
        'port': '2049', 'peerid': '7fc9015e-8134-4753-b837-54cbc6030c98',
        'ports': {'rdma': 'N/A', 'tcp': '2049'}}, '/bricks/brick1/a12':
        {'status': '1', 'pid': '30505', 'bricktype': 'hot', 'port': '49165',
        'peerid': '7fc9015e-8134-4753-b837-54cbc6030c98', 'ports': {'rdma':
        'N/A', 'tcp': '49165'}}}, '10.70.47.118': {'/bricks/brick1/a21':
        {'status': '1', 'pid': '5427', 'bricktype': 'cold', 'port': '49162',
        'peerid': '5397d8f5-2986-453a-b0b5-5c40a9bb87ff', 'ports': {'rdma':
        'N/A', 'tcp': '49162'}}, '/bricks/brick2/a41': {'status': '1', 'pid':
        '5446', 'bricktype': 'cold', 'port': '49163', 'peerid':
        '5397d8f5-2986-453a-b0b5-5c40a9bb87ff', 'ports': {'rdma': 'N/A',
        'tcp': '49163'}}, 'NFS Server': {'status': '1', 'pid': '6397', 'port':
        '2049', 'peerid': '5397d8f5-2986-453a-b0b5-5c40a9bb87ff', 'ports':
        {'rdma': 'N/A', 'tcp': '2049'}}}}}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster vol status %s %s %s --xml" % (volname, service, options)

    ret, out, _ = tc.run(mnode, cmd)
    if ret != 0:
        tc.logger.error("Failed to execute gluster volume status command")
        return None

    root = etree.XML(out)
    volume_list = _parse_volume_status_xml(root)
    if volume_list is None:
        tc.logger.error("No volumes exists in the gluster")
        return None

    vol_status = {}
    for volume in volume_list:
        tmp_dict1 = {}
        tmp_dict2 = {}
        hot_bricks = []
        cold_bricks = []
        vol_name = [vol.text for vol in volume if vol.tag == "volName"]

        # parsing volume status xml output
        if options == 'tasks':
            tasks = volume.findall("tasks")
            for each_task in tasks:
                tmp_dict3 = parse_xml(each_task)
                node_name = 'task_status'
                if 'task' in tmp_dict3.keys():
                    if node_name in tmp_dict2.keys():
                        tmp_dict2[node_name].append(tmp_dict3['task'])
                    else:
                        tmp_dict2[node_name] = [tmp_dict3['task']]
                else:
                    tmp_dict2[node_name] = [tmp_dict3]
        else:
            elem_tag = []
            for elem in volume.getchildren():
                elem_tag.append(elem.tag)
            if ('hotBricks' in elem_tag) or ('coldBricks' in elem_tag):
                for elem in volume.getchildren():
                    if (elem.tag == 'hotBricks'):
                        nodes = elem.findall("node")
                        hot_bricks = [node.find('path').text
                                      for node in nodes
                                      if (
                                       node.find('path').text.startswith('/'))]
                    if (elem.tag == 'coldBricks'):
                        for n in elem.findall("node"):
                            nodes.append(n)
                        cold_bricks = [node.find('path').text
                                       for node in nodes
                                       if (
                                        (node.find('path').
                                         text.startswith('/')))]
            else:
                nodes = volume.findall("node")

            for each_node in nodes:
                if each_node.find('path').text.startswith('/'):
                    node_name = each_node.find('hostname').text
                elif each_node.find('path').text == 'localhost':
                    node_name = mnode
                else:
                    node_name = each_node.find('path').text
                node_dict = parse_xml(each_node)
                tmp_dict3 = {}
                if "hostname" in node_dict.keys():
                    if node_dict['path'].startswith('/'):
                        if node_dict['path'] in hot_bricks:
                            node_dict["bricktype"] = 'hot'
                        elif node_dict['path'] in cold_bricks:
                            node_dict["bricktype"] = 'cold'
                        else:
                            node_dict["bricktype"] = 'None'
                        tmp = node_dict["path"]
                        tmp_dict3[node_dict["path"]] = node_dict
                    else:
                        tmp_dict3[node_dict["hostname"]] = node_dict
                        tmp = node_dict["hostname"]
                    del tmp_dict3[tmp]["path"]
                    del tmp_dict3[tmp]["hostname"]
                if node_name in tmp_dict1.keys():
                    tmp_dict1[node_name].append(tmp_dict3)
                else:
                    tmp_dict1[node_name] = [tmp_dict3]

                tmp_dict4 = {}
                for item in tmp_dict1[node_name]:
                    for key, val in item.items():
                        tmp_dict4[key] = val
                tmp_dict2[node_name] = tmp_dict4

        vol_status[vol_name[0]] = tmp_dict2
    tc.logger.debug("Volume status output: %s"
                    % pformat(vol_status, indent=10))
    return vol_status


def get_volume_option(volname, option='all', mnode=None):
    """gets the option values for the given volume.

    Args:
        volname (str): volume name

    Kwargs:
        option (str): volume option to get status.
                    If not given, the function returns all the options for
                    the given volume
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        dict: value for the given volume option in dict format, on success
        NoneType: on failure

    Example:
        get_volume_option("testvol")
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume get %s %s" % (volname, option)
    ret, out, _ = tc.run(mnode, cmd)
    if ret != 0:
        tc.logger.error("Failed to execute gluster volume get command"
                        "for volume %s" % volname)
        return None

    volume_option = {}
    raw_output = out.split("\n")
    for line in raw_output[2:-1]:
        match = re.search(r'^(\S+)(.*)', line.strip())
        if match is None:
            tc.logger.error("gluster get volume output is not in "
                            "expected format")
            return None

        volume_option[match.group(1)] = match.group(2).strip()

    return volume_option


def set_volume_options(volname, options, mnode=None):
    """sets the option values for the given volume.

    Args:
        volname (str): volume name
        options (dict): volume options in key
            value format

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        bool: True, if the volume option is set
              False, on failure

    Example:
        options = {"user.cifs":"enable","user.smb":"enable"}
        set_volume_option("testvol", options=options)
    """
    if mnode is None:
        mnode = tc.servers[0]
    _rc = True
    for option in options:
        cmd = ("gluster volume set %s %s %s"
               % (volname, option, options[option]))
        ret, _, _ = tc.run(mnode, cmd)
        if ret != 0:
            tc.logger.error("Unable to set value %s for option %s"
                            % (options[option], option))
            _rc = False
    return _rc


def volume_info(volname='all', mnode=None):
    """Executes gluster volume info cli command

    Kwargs:
        volname (str): volume name. Defaults to 'all'
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
        volume_status()
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume info %s" % volname
    return tc.run(mnode, cmd)


def get_volume_info(volname='all', mnode=None):
    """Fetches the volume information as displayed in the volume info.
        Uses xml output of volume info and parses the into to a dict

    Kwargs:
        volname (str): volume name. Defaults to 'all'
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: If there are errors
        dict: volume info in dict of dicts

    Example:
        get_volume_info(volname="testvol")
        >>>{'testvol': {'status': '1', 'xlators': None, 'disperseCount': '0',
        'bricks': {'coldBricks': {'colddisperseCount': '0',
        'coldarbiterCount': '0', 'coldBrickType': 'Distribute',
        'coldbrickCount': '4', 'numberOfBricks': '4', 'brick':
        [{'isArbiter': '0', 'name': '10.70.47.89:/bricks/brick1/a11',
        'hostUuid': '7fc9015e-8134-4753-b837-54cbc6030c98'}, {'isArbiter':
        '0', 'name': '10.70.47.118:/bricks/brick1/a21', 'hostUuid':
        '7fc9015e-8134-4753-b837-54cbc6030c98'}, {'isArbiter': '0', 'name':
        '10.70.47.89:/bricks/brick2/a31', 'hostUuid':
        '7fc9015e-8134-4753-b837-54cbc6030c98'}, {'isArbiter': '0',
        'name': '10.70.47.118:/bricks/brick2/a41', 'hostUuid':
        '7fc9015e-8134-4753-b837-54cbc6030c98'}], 'coldreplicaCount': '1'},
        'hotBricks': {'hotBrickType': 'Distribute', 'numberOfBricks': '1',
        'brick': [{'name': '10.70.47.89:/bricks/brick1/a12', 'hostUuid':
        '7fc9015e-8134-4753-b837-54cbc6030c98'}], 'hotbrickCount': '1',
        'hotreplicaCount': '1'}}, 'type': '5', 'distCount': '1',
        'replicaCount': '1', 'brickCount': '5', 'options':
        {'cluster.tier-mode': 'cache', 'performance.readdir-ahead': 'on',
        'features.ctr-enabled': 'on'}, 'redundancyCount': '0', 'transport':
        '0', 'typeStr': 'Tier', 'stripeCount': '1', 'arbiterCount': '0',
        'id': 'ffa8a8d1-546f-4ebf-8e82-fcc96c7e4e05', 'statusStr': 'Started',
        'optCount': '3'}}
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume info %s --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("volume info returned error")
        return None
    root = etree.XML(out)
    volinfo = {}
    for volume in root.findall("volInfo/volumes/volume"):
        for elem in volume.getchildren():
            if elem.tag == "name":
                volname = elem.text
                volinfo[volname] = {}
            elif elem.tag == "bricks":
                volinfo[volname]["bricks"] = {}
                tag_list = [x.tag for x in elem.getchildren() if x]
                if 'brick' in tag_list:
                    volinfo[volname]["bricks"]["brick"] = []
                for el in elem.getchildren():
                    if el.tag == 'brick':
                        brick_info_dict = {}
                        for elmt in el.getchildren():
                            brick_info_dict[elmt.tag] = elmt.text
                        (volinfo[volname]["bricks"]["brick"].
                         append(brick_info_dict))

                    if el.tag == "hotBricks" or el.tag == "coldBricks":
                        volinfo[volname]["bricks"][el.tag] = {}
                        volinfo[volname]["bricks"][el.tag]["brick"] = []
                        for elmt in el.getchildren():
                            if elmt.tag == 'brick':
                                brick_info_dict = {}
                                for el_brk in elmt.getchildren():
                                    brick_info_dict[el_brk.tag] = el_brk.text
                                (volinfo[volname]["bricks"][el.tag]["brick"].
                                 append(brick_info_dict))
                            else:
                                volinfo[volname]["bricks"][el.tag][elmt.tag] = elmt.text
            elif elem.tag == "options":
                volinfo[volname]["options"] = {}
                for option in elem.findall("option"):
                    for el in option.getchildren():
                        if el.tag == "name":
                            opt = el.text
                        if el.tag == "value":
                            volinfo[volname]["options"][opt] = el.text
            else:
                volinfo[volname][elem.tag] = elem.text

    tc.logger.debug("Volume info output: %s"
                    % pformat(volinfo, indent=10))

    return volinfo


def sync_volume(hostname, volname="all", mnode=None):
    """syncs the volume

    Args:
        hostname (str): host name

    Kwargs:
        volname (str): volume name. Defaults to 'all'.
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
        sync_volume("abc.xyz.com",volname="testvol")
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume sync %s %s --mode=script" % (hostname, volname)
    return tc.run(mnode, cmd)


def get_subvols(volname, mnode=None):
    """Gets the subvolumes in the given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        dict: with empty list values for all keys, if volume doesn't exist
        dict: Dictionary of subvols, value of each key is list of lists
            containing subvols
    Example:
        get_subvols("testvol")
    """

    subvols = {
        'hot_tier_subvols': [],
        'cold_tier_subvols': [],
        'volume_subvols': []
        }
    if mnode is None:
        mnode = tc.servers[0]
    volinfo = get_volume_info(volname, mnode)
    if volinfo is not None:
        voltype = volinfo[volname]['typeStr']
        if voltype == 'Tier':
            hot_tier_type = (volinfo[volname]["bricks"]
                             ['hotBricks']['hotBrickType'])
            tmp = volinfo[volname]["bricks"]['hotBricks']["brick"]
            hot_tier_bricks = [x["name"] for x in tmp if "name" in x]
            if (hot_tier_type == 'Distribute'):
                for brick in hot_tier_bricks:
                    subvols['hot_tier_subvols'].append([brick])

            elif (hot_tier_type == 'Replicate' or
                  hot_tier_type == 'Distributed-Replicate'):
                rep_count = int((volinfo[volname]["bricks"]['hotBricks']
                                ['numberOfBricks']).split("=", 1)[0].
                                split("x")[1].strip())
                subvol_list = ([hot_tier_bricks[i:i + rep_count]
                               for i in range(0, len(hot_tier_bricks),
                                rep_count)])
                subvols['hot_tier_subvols'] = subvol_list
            cold_tier_type = (volinfo[volname]["bricks"]['coldBricks']
                              ['coldBrickType'])
            tmp = volinfo[volname]["bricks"]['coldBricks']["brick"]
            cold_tier_bricks = [x["name"] for x in tmp if "name" in x]
            if (cold_tier_type == 'Distribute' or
                    cold_tier_type == 'Disperse'):
                for brick in cold_tier_bricks:
                    subvols['cold_tier_subvols'].append([brick])

            elif (cold_tier_type == 'Replicate' or
                  cold_tier_type == 'Distributed-Replicate'):
                rep_count = int((volinfo[volname]["bricks"]['coldBricks']
                                ['numberOfBricks']).split("=", 1)[0].
                                split("x")[1].strip())
                subvol_list = ([cold_tier_bricks[i:i + rep_count]
                               for i in range(0, len(cold_tier_bricks),
                                rep_count)])
                subvols['cold_tier_subvols'] = subvol_list

            elif (cold_tier_type == 'Distributed-Disperse'):
                disp_count = sum([int(nums) for nums in
                                 ((volinfo[volname]["bricks"]['coldBricks']
                                  ['numberOfBricks']).split("x", 1)[1].
                                  strip().split("=")[0].strip().strip("()").
                                  split()) if nums.isdigit()])
                subvol_list = [cold_tier_bricks[i:i + disp_count]
                               for i in range(0, len(cold_tier_bricks),
                                              disp_count)]
                subvols['cold_tier_subvols'] = subvol_list
            return subvols

        tmp = volinfo[volname]["bricks"]["brick"]
        bricks = [x["name"] for x in tmp if "name" in x]
        if voltype == 'Replicate' or voltype == 'Distributed-Replicate':
            rep_count = int(volinfo[volname]['replicaCount'])
            subvol_list = [bricks[i:i + rep_count]for i in range(0,
                                                                 len(bricks),
                                                                 rep_count)]
            subvols['volume_subvols'] = subvol_list
        elif voltype == 'Distribute' or voltype == 'Disperse':
            for brick in bricks:
                subvols['volume_subvols'].append([brick])

        elif voltype == 'Distributed-Disperse':
            disp_count = int(volinfo[volname]['disperseCount'])
            subvol_list = [bricks[i:i + disp_count]for i in range(0,
                                                                  len(bricks),
                                                                  disp_count)]
            subvols['volume_subvols'] = subvol_list
    return subvols
