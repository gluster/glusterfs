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
           end_str_to_parse + "/{e=NR}END{for(i=s;i<=e;++i)print a[i]}' "
           + filename)

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


def get_pathinfo(filename, volname, client=None):
    """This module gets filepath of the given file in gluster server.

    Example:
        get_pathinfo("file1", "testvol")

    Args:
        filename (str): relative path of file
        volname (str): volume name

    Kwargs:
        client (str): client on which cmd has to be executed.

    Returns:
        NoneType: None if command execution fails, parse errors.
        list: file path for the given file in gluster server
    """

    if client is None:
        client = tc.clients[0]

    server = get_volume_info(volname)[volname]['bricks'][0].split(':')[0]
    mount_point = '/mnt/tmp_fuse'

    #Performing glusterfs mount because only with glusterfs mount
    #the file location in gluster server can be identified from client
    #machine
    ret, _, _ = mount_volume(volname, mtype='glusterfs',
                             mpoint=mount_point,
                             mserver=server,
                             mclient=client)
    if ret != 0:
        tc.logger.error("Failed to do gluster mount on volume %s to fetch"
                        "pathinfo from client %s"
                        % (volname, client))
        return None

    filename = mount_point + '/' + filename
    attr_name = 'trusted.glusterfs.pathinfo'
    output = get_extended_attributes_info([filename],
                                          attr_name=attr_name,
                                          mnode=client)
    if output is None:
        tc.logger.error("Failed to get path info for %s" % filename)
        return None

    pathinfo = output[filename][attr_name]

    umount_volume(client, mount_point)

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
