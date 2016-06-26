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
    Description: Helper library for gluster modules.
"""

from distaf.util import tc
from distaflibs.gluster.volume_ops import get_volume_info
from distaflibs.gluster.mount_ops import mount_volume, umount_volume
import re
import time
from collections import OrderedDict
import tempfile

try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree


def append_string_to_file(mnode, filename, str_to_add_in_file):
    """Appends the given string in the file.

    Example:
        append_string_to_file("abc.def.com", "/var/log/messages",
                           "test_1_string")

    Args:
        mnode (str): Node on which cmd has to be executed.
        filename (str): absolute file path to append the string
        str_to_add_in_file (str): string to be added in the file,
            which is used as a start and stop string for parsing
            the file in search_pattern_in_file().

    Returns:
        True, on success, False otherwise
    """
    try:
        conn = tc.get_connection(mnode, 'root')
        if conn == -1:
            tc.logger.error("Unable to get connection to 'root' of node %s"
                            " in append_string_to_file()" % mnode)
            return False

        with conn.builtin.open(filename, 'a') as _filehandle:
            _filehandle.write(str_to_add_in_file)

        return True
    except:
        tc.logger.error("Exception occured while adding string to "
                        "file %s in append_string_to_file()" % filename)
        return False
    finally:
        conn.close()


def search_pattern_in_file(mnode, search_pattern, filename, start_str_to_parse,
                           end_str_to_parse):
    """checks if the given search pattern exists in the file
       in between 'start_str_to_parse' and 'end_str_to_parse' string.

    Example:
        search_pattern = r'.*scrub.*'
        search_log("abc.def.com", search_pattern, "/var/log/messages",
                    "start_pattern", "end_pattern")

    Args:
        mnode (str): Node on which cmd has to be executed.
        search_pattern (str): regex string to be matched in the file
        filename (str): absolute file path to search given string
        start_str_to_parse (str): this will be as start string in the
            file from which this method will check
            if the given search string is present.
        end_str_to_parse (str): this will be as end string in the
            file whithin which this method will check
            if the given search string is present.

    Returns:
        True, if search_pattern is present in the file
        False, otherwise
    """

    cmd = ("awk '{a[NR]=$0}/" + start_str_to_parse + "/{s=NR}/" +
           end_str_to_parse + "/{e=NR}END{for(i=s;i<=e;++i)print "
           "a[i]}' " + filename)

    ret, out, err = tc.run(mnode, cmd)
    if ret != 0:
        tc.logger.error("Failed to match start and end pattern in file"
                        % filename)
        return False

    if not re.search(search_pattern, str(out), re.S):
        tc.logger.error("file %s did not have the expected message"
                        % filename)
        return False

    return True


def calculate_checksum(file_list, chksum_type='sha256sum', mnode=None):
    """This module calculates given checksum for the given file list

    Example:
        calculate_checksum([file1, file2])

    Args:
        file_list (list): absolute file names for which checksum
            to be calculated

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
        chksum_type (str): type of the checksum algorithm.
            Defaults to sha256sum

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: checksum value for each file in the given file list
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = chksum_type + " %s" % ' '.join(file_list)
    ret = tc.run(mnode, cmd)
    if ret[0] != 0:
        tc.logger.error("Failed to execute checksum command in server %s"
                        % mnode)
        return None

    checksum_dict = {}
    for line in ret[1].split('\n')[:-1]:
        match = re.search(r'^(\S+)\s+(\S+)', line.strip())
        if match is None:
            tc.logger.error("checksum output is not in"
                            "expected format")
            return None

        checksum_dict[match.group(2)] = match.group(1)

    return checksum_dict


def get_extended_attributes_info(file_list, encoding='hex', attr_name='',
                                 mnode=None):
    """This module gets extended attribute info for the given file list

    Example:
        get_extended_attributes_info([file1, file2])

    Args:
        file_list (list): absolute file names for which extended
            attributes to be fetched

    Kwargs:
        encoding (str): encoding format
        attr_name (str): extended attribute name
        mnode (str): Node on which cmd has to be executed.

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: extended attribute for each file in the given file list
    """

    if mnode is None:
        mnode = tc.servers[0]

    if attr_name == '':
        cmd = "getfattr -d -m . -e %s %s" % (encoding, ' '.join(file_list))
    else:
        cmd = "getfattr -d -m . -n %s %s" % (attr_name, ' '.join(file_list))

    ret = tc.run(mnode, cmd)
    if ret[0] != 0:
        tc.logger.error("Failed to execute getfattr command in server %s"
                        % mnode)
        return None

    attr_dict = {}
    for each_attr in ret[1].split('\n\n')[:-1]:
        for line in each_attr.split('\n'):
            if line.startswith('#'):
                match = re.search(r'.*file:\s(\S+).*', line)
                if match is None:
                    tc.logger.error("getfattr output is not in "
                                    "expected format")
                    return None
                key = "/" + match.group(1)
                attr_dict[key] = {}
            else:
                output = line.split('=')
                attr_dict[key][output[0]] = output[1]
    return attr_dict


def get_pathinfo(filename, volname, mnode=None):
    """This module gets filepath of the given file in gluster server.

    Example:
        get_pathinfo("file1", "testvol")

    Args:
        filename (str): relative path of file
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed. Defaults
            to tc.servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: file path for the given file in gluster server
    """

    if mnode is None:
        mnode = tc.servers[0]

    mount_point = tempfile.mkdtemp()

    # Performing glusterfs mount because only with glusterfs mount
    # the file location in gluster server can be identified
    ret, _, _ = mount_volume(volname, mtype='glusterfs',
                             mpoint=mount_point,
                             mserver=mnode,
                             mclient=mnode)
    if ret != 0:
        tc.logger.error("Failed to do gluster mount on volume %s to fetch"
                        "pathinfo from server %s"
                        % (volname, mnode))
        return None

    filename = mount_point + '/' + filename
    attr_name = 'trusted.glusterfs.pathinfo'
    output = get_extended_attributes_info([filename],
                                          attr_name=attr_name,
                                          mnode=mnode)
    if output is None:
        tc.logger.error("Failed to get path info for %s" % filename)
        return None

    pathinfo = output[filename][attr_name]

    umount_volume(mnode, mount_point)
    tc.run(mnode, "rm -rf " + mount_point)

    return re.findall(".*?POSIX.*?:(\S+)\>", pathinfo)


def list_files(dir_path, parse_str="", mnode=None):
    """This module list files from the given file path

    Example:
        list_files("/root/dir1/")

    Args:
        dir_path (str): directory path name

    Kwargs:
        parse_str (str): sub string of the filename to be fetched
        mnode (str): Node on which cmd has to be executed.

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: files with absolute name
    """
    if mnode is None:
        mnode = tc.clients[0]

    try:
        conn = tc.get_connection(mnode, 'root')
        if conn == -1:
            tc.logger.error("Unable to get connection to 'root' of node %s"
                            % mnode)
            return None

        filepaths = []
        for root, directories, files in conn.modules.os.walk(dir_path):
            for filename in files:
                if parse_str != "":
                    if parse_str in filename:
                        filepath = conn.modules.os.path.join(root, filename)
                        filepaths.append(filepath)
                else:
                    filepath = conn.modules.os.path.join(root, filename)
                    filepaths.append(filepath)
        return filepaths
    except:
        tc.logger.error("Exception occured in list_files()")
        return None

    finally:
        conn.close()


def get_servers_bricks_dict(servers):
    """This module returns servers_bricks dictionary.
    Args:
        servers (list): List of servers for which we need the
            list of bricks available on it.
    Returns:
        OrderedDict: key - server
              value - list of bricks
    Example:
        get_servers_bricks_dict(tc.servers)
    """
    servers_bricks_dict = OrderedDict()
    if not isinstance(servers, list):
        servers = [servers]
    for server in servers:
        for server_list in tc.global_config["servers"]:
            if server_list["host"] == server:
                brick_root = server_list["brick_root"]
                ret, out, err = tc.run(server, "cat /proc/mounts | grep %s"
                                       " | awk '{ print $2}'" % brick_root)
                if ret != 0:
                    tc.logger.error("bricks not available on %s" % server)
                else:
                    servers_bricks_dict[server] = out.strip().split("\n")

    for key, value in servers_bricks_dict.items():
        value.sort()

    return servers_bricks_dict


def get_servers_used_bricks_dict(servers, mnode):
    """This module returns servers_used_bricks dictionary.
       This information is fetched from gluster volume info command.
    Args:
        servers (list): List of servers for which we need the
            list of unused bricks on it.
        mnode (str): The node on which gluster volume info command has
            to be executed.
    Returns:
        OrderedDict: key - server
              value - list of used bricks
                      or empty list(if all bricks are free)
    Example:
        get_servers_used_bricks_dict(tc.servers[:], tc.servers[0])
    """
    if not isinstance(servers, list):
        servers = [servers]

    servers_used_bricks_dict = OrderedDict()
    for server in servers:
        servers_used_bricks_dict[server] = []

    ret, out, err = tc.run(mnode, "gluster volume info | egrep "
                           "\"^Brick[0-9]+\" | grep -v \"ss_brick\"",
                           verbose=False)
    if ret != 0:
        tc.logger.error("error in getting bricklist using gluster v info")
    else:
        list1 = list2 = []
        list1 = out.strip().split('\n')
        for item in list1:
            x = re.search(':(.*)/(.*)', item)
            list2 = x.group(1).strip().split(':')
            if servers_used_bricks_dict.has_key(list2[0]):
                value = servers_used_bricks_dict[list2[0]]
                value.append(list2[1])
            else:
                servers_used_bricks_dict[list2[0]] = [list2[1]]

    for key, value in servers_used_bricks_dict.items():
        value.sort()

    return servers_used_bricks_dict


def get_servers_unused_bricks_dict(servers, mnode):
    """This module returns servers_unused_bricks dictionary.
    Gets a list of unused bricks for each server by using functions,
    get_servers_bricks_dict() and get_servers_used_bricks_dict()
    Args:
        servers (list): List of servers for which we need the
            list of unused bricks available on it.
        mnode (str): The node on which gluster volume info command has
            to be executed.
     Returns:
        OrderedDict: key - server
              value - list of unused bricks
    Example:
        get_servers_unused_bricks_dict(tc.servers, tc.servers[0])
    """
    if not isinstance(servers, list):
        servers = [servers]
    dict1 = get_servers_bricks_dict(servers)
    dict2 = get_servers_used_bricks_dict(servers, mnode)
    servers_unused_bricks_dict = OrderedDict()
    for key, value in dict1.items():
        if dict2.has_key(key):
            unused_bricks = list(set(value) - set(dict2[key]))
            servers_unused_bricks_dict[key] = unused_bricks
        else:
            servers_unused_bricks_dict[key] = value

    for key, value in servers_unused_bricks_dict.items():
        value.sort()

    return servers_unused_bricks_dict


def form_bricks_path(number_of_bricks, servers, mnode, volname):
    """Forms complete bricks path for create-volume/add-brick
       given the num_of_bricks
    Args:
        number_of_bricks (int): The number of bricks for which brick list
            has to be created.
        servers (list): The list of servers from which the bricks
            needs to be selected for creating the brick list.
        mnode (str): The node on which the command has to be run.
        volname (str): Volume name for which we require brick-list.
    Returns:
        str - complete brick path.
        None - if number_of_bricks is greater than unused bricks.
    Example:
        form_bricks_path(6, tc.servers, tc.servers(0), "testvol")
    """
    if not isinstance(servers, list):
        servers = [servers]
    dict_index = 0
    bricks_path = ''

    server_bricks_dict = get_servers_unused_bricks_dict(servers, servers[0])
    num_of_unused_bricks = 0
    for server_brick in server_bricks_dict.values():
        num_of_unused_bricks = num_of_unused_bricks + len(server_brick)

    if num_of_unused_bricks < number_of_bricks:
        tc.logger.error("Not enough bricks available for creating the bricks")
        return None

    brick_index = 0
    vol_info_dict = get_volume_info(volname, mnode)
    if vol_info_dict:
        brick_index = int(vol_info_dict[volname]['brickCount'])

    for num in range(brick_index, brick_index + number_of_bricks):
        if server_bricks_dict.values()[dict_index]:
            bricks_path = ("%s %s:%s/%s_brick%s" % (bricks_path,
                           server_bricks_dict.keys()[dict_index],
                           server_bricks_dict.values()[dict_index][0],
                           volname, num))
            server_bricks_dict.values()[dict_index].pop(0)
        if dict_index < len(server_bricks_dict) - 1:
            dict_index = dict_index + 1
        else:
            dict_index = 0

    return bricks_path
