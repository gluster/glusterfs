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


def start_volume(volname, mnode='', force=False):
    """
        Starts the gluster volume
        Returns True if success and False if failure
    """
    if mnode == '':
        mnode = tc.servers[0]
    frce = ''
    if force:
        frce = 'force'
    ret = tc.run(mnode, "gluster volume start %s %s" % (volname, frce))
    if ret[0] != 0:
        return False
    return True


def stop_volume(volname, mnode='', force=False):
    """
        Stops the gluster volume
        Returns True if success and False if failure
    """
    if mnode == '':
        mnode = tc.servers[0]
    frce = ''
    if force:
        frce = 'force'
    ret = tc.run(mnode, "gluster volume stop %s %s --mode=script" \
            % (volname, frce))
    if ret[0] != 0:
        return False
    return True


def delete_volume(volname, mnode=''):
    """
        Deletes the gluster volume
        Returns True if success and False if failure
    """
    if mnode == '':
        mnode = tc.servers[0]
    volinfo = get_volume_info(volname, mnode)
    if volinfo is None or volname not in volinfo:
        tc.logger.info("Volume %s does not exist in %s" % (volname, mnode))
        return True
    bricks = volinfo[volname]['bricks']
    ret = tc.run(mnode, "gluster volume delete %s --mode=script" % volname)
    if ret[0] != 0:
        return False
    try:
        del tc.global_flag[volname]
    except KeyError:
        pass
    for brick in bricks:
        node, vol_dir = brick.split(":")
        ret = tc.run(node, "rm -rf %s" % vol_dir)

    return True


def reset_volume(volname, mnode='', force=False):
    """
        Reset the gluster volume
        Returns True if success and False if failure
    """
    if mnode == '':
        mnode = tc.servers[0]
    frce = ''
    if force:
        frce = 'force'
    ret = tc.run(mnode, "gluster volume reset %s %s --mode=script" \
            % (volname, frce))
    if ret[0] != 0:
        return False
    return True


def cleanup_volume(volname, mnode=''):
    """
        stops and deletes the volume
        returns True on success and False otherwise

        TODO: Add snapshot cleanup part here
    """
    if mnode == '':
        mnode = tc.servers[0]
    ret = stop_volume(volname, mnode, True) | \
            delete_volume(volname, mnode)
    if not ret:
        tc.logger.error("Unable to cleanup the volume %s" % volname)
        return False
    return True


def setup_meta_vol(servers=''):
    """
        Creates, starts and mounts the gluster meta-volume on the servers
        specified.
    """
    if servers == '':
        servers = tc.servers
    meta_volname = 'gluster_shared_storage'
    mount_point = '/var/run/gluster/shared_storage'
    metav_dist = int(tc.config_data['META_VOL_DIST_COUNT'])
    metav_rep = int(tc.config_data['META_VOL_REP_COUNT'])
    _num_bricks = metav_dist * metav_rep
    repc = ''
    if metav_rep > 1:
        repc = "replica %d" % metav_rep
    bricks = ''
    brick_root = "/bricks"
    _n = 0
    for i in range(0, _num_bricks):
        bricks = "%s %s:%s/%s_brick%d" % (bricks, servers[_n], \
                brick_root, meta_volname, i)
        if _n < len(servers) - 1:
            _n = _n + 1
        else:
            _n = 0
    gluster_cmd = "gluster volume create %s %s %s force" \
            % (meta_volname, repc, bricks)
    ret = tc.run(servers[0], gluster_cmd)
    if ret[0] != 0:
        tc.logger.error("Unable to create meta volume")
        return False
    ret = start_volume(meta_volname, servers[0])
    if not ret:
        tc.logger.error("Unable to start the meta volume")
        return False
    time.sleep(5)
    for server in servers:
        ret = mount_volume(meta_volname, 'glusterfs', mount_point, server, \
                server)
        if ret[0] != 0:
            tc.logger.error("Unable to mount meta volume on %s" % server)
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
    This module takes any xml element object and parses all the child nodes
    and returns the parsed data in dictionary format
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


def get_volume_status(volname='all', service='', options='', mnode=''):
    """
    This module gets the status of all or specified volume(s)/brick
    @parameter:
        * mnode  - <str> (optional) name of the node to execute the volume
                    status command. If not given, the function takes the
                    first node from config file
        * volname - <str> (optional) name of the volume to get status. It not
                    given, the function returns the status of all volumes
        * service - <str> (optional) name of the service to get status.
                    serivce can be, [nfs|shd|<BRICK>|quotad]], If not given,
                    the function returns all the services
        * options - <str> (optional) options can be,
                    [detail|clients|mem|inode|fd|callpool|tasks]. If not given,
                    the function returns the output of gluster volume status
    @Returns: volume status in dict of dictionary format, on success
              None, on failure
    """

    if mnode == '':
        mnode = tc.servers[0]

    cmd = "gluster vol status %s %s %s --xml" % (volname, service, options)

    ret = tc.run(mnode, cmd)
    if ret[0] != 0:
        tc.logger.error("Failed to execute gluster volume status command")
        return None

    root = etree.XML(ret[1])
    volume_list = _parse_volume_status_xml(root)
    if volume_list is None:
        tc.logger.error("No volumes exists in the gluster")
        return None

    vol_status = {}
    for volume in volume_list:
        tmp_dict1 = {}
        tmp_dict2 = {}
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
            nodes = volume.findall("node")
            for each_node in nodes:
                if each_node.find('path').text.startswith('/'):
                    node_name = each_node.find('hostname').text
                else:
                    node_name = each_node.find('path').text
                node_dict = parse_xml(each_node)
                tmp_dict3 = {}
                if "hostname" in node_dict.keys():
                    if node_dict['path'].startswith('/'):
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
    tc.logger.debug("Volume status output: %s" \
                    % pformat(vol_status, indent=10))
    return vol_status


def get_volume_option(volname, option='all', server=''):
    """
    This module gets the option values for the given volume.
    @parameter:
        * volname - <str> name of the volume to get status.
        * option  - <str> (optional) name of the volume option to get status.
                    If not given, the function returns all the options for
                    the given volume
        * server  - <str> (optional) name of the server to execute the volume
                    status command. If not given, the function takes the
                    first node from config file
    @Returns: value for the given volume option in dict format, on success
              None, on failure
    """
    if server == '':
        server = tc.servers[0]

    cmd = "gluster volume get %s %s" % (volname, option)
    ret = tc.run(server, cmd)
    if ret[0] != 0:
        tc.logger.error("Failed to execute gluster volume get command")
        return None

    volume_option = {}
    raw_output = ret[1].split("\n")
    for line in raw_output[2:-1]:
        match = re.search(r'^(\S+)(.*)', line.strip())
        if match is None:
            tc.logger.error("gluster get volume output is not in \
                             expected format")
            return None

        volume_option[match.group(1)] = match.group(2).strip()

    return volume_option


def get_volume_info(volname='all', server=''):
    """
        Fetches the volume information as displayed in the volume info.
        Uses xml output of volume info and parses the into to a dict

        Returns a dict of dicts.
        -- Volume name is the first key
        -- distCount/replicaCount/Type etc are second keys
        -- The value of the each second level dict depends on the key
        -- For distCount/replicaCount etc the value is key
        -- For bricks, the value is a list of bricks (hostname:/brick_path)
    """
    if server == '':
        server = tc.servers[0]
    ret = tc.run(server, "gluster volume info %s --xml" % volname, \
            verbose=False)
    if ret[0] != 0:
        tc.logger.error("volume info returned error")
        return None
    root = etree.XML(ret[1])
    volinfo = {}
    for volume in root.findall("volInfo/volumes/volume"):
        for elem in volume.getchildren():
            if elem.tag == "name":
                volname = elem.text
                volinfo[volname] = {}
            elif elem.tag == "bricks":
                volinfo[volname]["bricks"] = []
                for el in elem.getiterator():
                    if el.tag == "name":
                        volinfo[volname]["bricks"].append(el.text)
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
    return volinfo


def set_volume_option(volname, options, server=''):
    """
    This module sets the option values for the given volume.
    @parameter:
        * volname - <str> name of the volume to get status.
        * option  - list of <dict> volume options in key value format.
        * server  - <str> (optional) name of the server to execute the volume
                    status command. If not given, the function takes the
                    first node from config file
    @Returns: True, on success
              False, on failure
    """
    if server == '':
        server = tc.servers[0]
    _rc = True
    for option in options:
        cmd = "gluster volume set %s %s %s" % (volname, option, \
                options[option])
        ret = tc.run(server, cmd)
        if ret[0] != 0:
            _rc = False
    return _rc
