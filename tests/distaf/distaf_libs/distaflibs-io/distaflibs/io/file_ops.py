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
Description: Library for file operations.
"""


import re
import socket
from distaf.util import tc


def write_file(filename, file_contents=" ", create_mode='', filesize='', \
        server=''):
    """
    This module writes the file along with file contents
    @paramater:
        * filename - <str> absolute path name of the file to be created
        * file_contents - <str> (optional) file content
        * create_mode - <str> (optional) mode to create the file
        * filesize - <str> (optional) filesize
        * server   - <str> (optional) name of the server to write the
                     file. If not given, the function takes the
                     first node from config file
    @Returns: True, on success
              False, on failure
    """
    if server == '':
        server = tc.servers[0]

    if create_mode == '':
        create_mode = 'open'

    if create_mode != 'open':
        try:
            conn = tc.get_connection(server, 'root')
            if conn == -1:
                tc.logger.error("Unable to get connection to 'root' of "
                                "node %s" % server)
                return False
            if not conn.modules.os.path.exists(conn.modules.os.path.\
                    dirname(filename)):
                conn.modules.os.makedirs(conn.modules.os.path.\
                        dirname(filename))
        except:
            tc.logger.error("Exception occured while creating directory  for "
                            "file %s" % filename)
            return False
        finally:
            conn.close()

    if create_mode == 'open':
        try:
            conn = tc.get_connection(server, 'root')
            if conn == -1:
                tc.logger.error("Unable to get connection to 'root' of node "
                                "%s" % server)
                return False

            if not conn.modules.os.path.exists(conn.modules.os.path.\
                    dirname(filename)):
                conn.modules.os.makedirs(conn.modules.os.path.\
                        dirname(filename))

            with conn.builtin.open(filename, 'w') as _filehandle:
                _filehandle.write(file_contents)
        except:
            tc.logger.error("Exception occured while writing file %s" \
                    % filename)
            return False

        finally:
            conn.close()
    elif create_mode == 'echo':
        cmd = "echo " + file_contents + " > " + filename
        ret, _, _ = tc.run(server, cmd)
        if ret != 0:
            return False
    elif create_mode == 'touch':
        cmd = "touch " + filename
        ret, _, _ = tc.run(server, cmd)
        if ret != 0:
            return False
    elif create_mode == 'dd':
        if filesize == '':
            tc.logger.error("Invalid argument for dd cmd. Pass correct \
                             number of parameters")
            return False

        cmd = "dd if=/dev/zero of=%s bs=%s count=1" % (filename, filesize)
        ret, _, _ = tc.run(server, cmd)
        if ret != 0:
            return False

    return True


def remove_file(filename, server=''):
    """
    This module removes the given file
    @paramater:
        * filename - <str> absolute path name of the file to be created
        * server   - <str> (optional) name of the server to write the
                     file. If not given, the function takes the
                     first node from config file
    @Returns: True, on success
              False, on failure
    """
    if server == '':
        server = tc.servers[0]
    try:
        conn = tc.get_connection(server, 'root')
        if conn == -1:
            tc.logger.error("Unable to get connection to 'root' of node %s" \
                            % server)
            return False
        if conn.modules.os.path.exists(filename):
            conn.modules.os.remove(filename)
    except:
        tc.logger.error("Exception occured while removing file %s" % filename)
        return False

    finally:
        conn.close()

    return True


def calculate_checksum(file_list, server=''):
    """
    This module calculates checksum (sha256sum) for the given file list
    @paramater:
        * file_list - <list> absolute file names for which checksum to be
                      calculated
        * server    - <str> (optional) name of the server.
                      If not given, the function takes the
                      first node from config file
    @Returns: checksum value in dict format, on success
              None, on failure
    """
    if server == '':
        server = tc.servers[0]

    cmd = "sha256sum %s" % ' '.join(file_list)
    ret = tc.run(server, cmd)
    if ret[0] != 0:
        tc.logger.error("Failed to execute checksum command in server %s" \
                         % server)
        return None

    checksum_dict = {}
    for line in ret[1].split('\n')[:-1]:
        match = re.search(r'^(\S+)\s+(\S+)', line.strip())
        if match is None:
            tc.logger.error("checksum output is not in \
                             expected format")
            return None

        checksum_dict[match.group(2)] = match.group(1)

    return checksum_dict


def get_extended_attributes_info(file_list, encoding='hex', attr_name='', \
        server=''):
    """
    This module gets extended attribute info for the given file list
    @paramater:
        * file_list - <list> absolute file names
        * encoding  - <str> (optional) encoding format
        * server    - <str> (optional) name of the server.
                      If not given, the function takes the
                      first node from config file
    @Returns: Extended attribute info in dict format, on success
              None, on failure
    """
    if server == '':
        server = tc.servers[0]

    server = socket.gethostbyname(server)
    if attr_name == '':
        cmd = "getfattr -d -m . -e %s %s" % (encoding, ' '.join(file_list))
    else:
        cmd = "getfattr -d -m . -n %s %s" % (attr_name, ' '.join(file_list))

    ret = tc.run(server, cmd)
    if ret[0] != 0:
        tc.logger.error("Failed to execute getfattr command in server %s" \
                         % server)
        return None

    attr_dict = {}
    for each_attr in ret[1].split('\n\n')[:-1]:
        for line in each_attr.split('\n'):
            if line.startswith('#'):
                match = re.search(r'.*file:\s(\S+).*', line)
                if match is None:
                    tc.logger.error("getfattr output isn't in expected format")
                    return None
                key = "/" + match.group(1)
                attr_dict[key] = {}
            else:
                output = line.split('=')
                attr_dict[key][output[0]] = output[1]
    return attr_dict


def get_filepath_from_rhsnode(filename, server=''):
    """
    This module gets filepath of the given file from rhsnode
    @paramater:
        * filename  - <str> absolute name of the file
        * server    - <str> (optional) name of the server.
                      If not given, the function takes the
                      first client from config file
    @Returns: file path for the given file in rhs node in list format
              on success None, on failure
    """
    if server == '':
        server = tc.clients[0]

    output = get_extended_attributes_info([filename], \
            attr_name='trusted.glusterfs.pathinfo', server=server)
    if output is None:
        tc.logger.error("Failed to get path info")
        return None

    pathinfo = output[filename]['trusted.glusterfs.pathinfo']
    return re.findall(".*?POSIX.*?:(\S+)\>", pathinfo)
