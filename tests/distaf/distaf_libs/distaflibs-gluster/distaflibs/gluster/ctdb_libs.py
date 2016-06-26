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
    Description: Library for setting up CTDB on gluster setup
"""
import time
import re
from distaf.util import tc
from distaflibs.gluster.volume_ops import start_volume
from distaflibs.gluster.peer_ops import peer_probe_servers


def ctdb_firewall_settings(servers=None):
    '''Firewall settings required for setting up CTDB
    Kwargs:
        servers (list): The list of servers on which we need
            to add firewall settings for setting up CTDB.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        ctdb_firewall_settings()
    '''
    if servers is None:
        servers = tc.global_config['cluster_config']['smb']['ctdb_servers']
    if not isinstance(servers, list):
        servers = [servers]
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    _rc = True
    for server in servers:
        ret, out, _ = tc.run(server, "cat /etc/redhat-release")
        if not ret:
            match = re.search('.*release (\d+).*', out)
            if match is None:
                tc.logger.error("Failed to find OS version")
                return False
            if match.group(1) == '7':
                ret, out, err = tc.run(server, "service firewalld start")
                if ret != 0:
                    tc.logger.error("Failed to start firewalld")
                ret, _, _ = tc.run(server, "firewall-cmd --zone=public "
                                   "--add-service=samba --add-service="
                                   "glusterfs")
                if ret != 0:
                    tc.logger.error("Failed to set firewall zone for samba")
                    _rc = False
                ret, _, _ = tc.run(server, "firewall-cmd --zone=public "
                                   "--add-service=samba --add-service="
                                   "glusterfs --permanent")
                if ret != 0:
                    tc.logger.error("Failed to set firewall zone for samba"
                                    " permanently")
                    _rc = False

                ret, _, _ = tc.run(server, "firewall-cmd --zone=public "
                                   "--add-port=4379/tcp")
                if ret != 0:
                    tc.logger.error("Failed to add port for samba")
                    _rc = False
                ret, _, _ = tc.run(server, "firewall-cmd --zone=public "
                                   "--add-port=4379/tcp --permanent")
                if ret != 0:
                    tc.logger.error("Failed to add port for samba permanently")
                    _rc = False
    return _rc


def update_smb_conf(servers=None):
    '''Update the /etc/samba/smb.conf file.
       Adds a parameter called clustering=yes.
    Kwargs:
        servers (list): The list of servers on which we need
            to update the /etc/samba/smb.conf file.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        update_smb_conf()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
    file_path = "/etc/samba/smb.conf"
    if not isinstance(servers, list):
        servers = [servers]
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    _rc = True
    for server in servers:
        ret, _, _ = tc.run(server, "grep 'clustering=yes' %s" % file_path)
        if ret == 0:
            tc.logger.info("%s file is already edited")
            continue
        ret, _, _ = tc.run(server, "sed -i '/^\[global\]/a clustering=yes' %s"
                           % file_path)
        if ret != 0:
            tc.logger.error("failed to edit %s file on %s"
                            % (file_path, server))
            _rc = False

    return _rc


def update_hook_scripts(servers=None):
    '''Update the hook scripts.
       Changes the META parameter value from "all" to "ctdb".
    Kwargs:
        servers (list): The list of servers on which we need
            to update the hook scripts.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        update_hook_scripts()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    file1_path = "/var/lib/glusterd/hooks/1/start/post/S29CTDBsetup.sh"
    file2_path = "/var/lib/glusterd/hooks/1/stop/pre/S29CTDB-teardown.sh"
    _rc = True
    if not isinstance(servers, list):
        servers = [servers]
    for server in servers:
        ret, _, _ = tc.run(server, "sed -i s/'META=\"all\"'/'META=\"ctdb\"'/g"
                           " %s %s" % (file1_path, file2_path))
        if ret != 0:
            tc.logger.error("failed to edit %s hook-script on %s"
                            % (file_path, server))
            _rc = False

    return _rc


def create_ctdb_nodes_file(servers=None):
    '''Creates the /etc/ctdb/nodes file.
       This file will have a list of IPs of all the servers.
    Kwargs:
        servers (list): The list of servers on which we need
            to update the /etc/ctdb/nodes file.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        create_ctdb_nodes_file()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    file_path = "/etc/ctdb/nodes"
    _rc = True
    ctdb_ips_list = []
    if not isinstance(servers, list):
        servers = [servers]
    for server in servers:
        ret, out, _ = tc.run(server, "hostname -i")
        if ret != 0:
            tc.logger.error("failed to get ip of %s" % server)
            _rc = False
        ctdb_ips_list.append(out)
    for server in servers:
        conn = tc.get_connection(server, "root")
        try:
            FH = conn.builtin.open(file_path, "w")
            FH.write("".join(ctdb_ips_list))
        except IOError as e:
            tc.logger.error(e)
            _rc = False
        finally:
            FH.close()

    return _rc


def create_ctdb_meta_volume(mnode=None, servers=None, meta_volname=None):
    '''Creates meta volume for ctdb setup
    Kwargs:
        mnode (str): Node on which the command has
            to be executed. Default value is ctdb_servers[0].
        servers (list): The list of servers on which we need
            the meta volume to be created.
            Defaults to ctdb_servers as specified in the config file.
        meta_volname (str) : Name for the ctdb meta volume.
            Dafault ctdb meta volume name is "ctdb".
    Returns:
        bool: True if successful, False otherwise
    Example:
        create_ctdb_meta_volume()
    '''
    if mnode is None:
        mnode = (tc.global_config['gluster']['cluster_config']
                 ['smb']['ctdb_servers'][0]['host'])
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    if meta_volname is None:
        meta_volname = "ctdb"
    replica_count = len(servers)
    brick_list = ""
    brick_path = (tc.global_config['gluster']['cluster_config']
                  ['smb']['ctdb_metavol_brick_path'])
    if not isinstance(servers, list):
        servers = [servers]
    for i, server in enumerate(servers):
        brick_list = (brick_list + "%s:%s/ctdb_brick%s "
                      % (server, brick_path, i))
    ret, _, _ = tc.run(mnode, "gluster volume create %s replica %s %s force "
                       "--mode=script" % (meta_volname, replica_count,
                                          brick_list))
    if ret != 0:
        tc.logger.error("failed to create meta volume ctdb")
        return False

    return True


def check_if_gluster_lock_mount_exists(servers=None):
    '''Checks if /gluster/lock mount exists
    Kwargs:
        servers (list): The list of servers on which we need
            to check if /gluster/lock mount exists.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        check_if_gluster_lock_mount_exists()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    if not isinstance(servers, list):
        servers = [servers]
    _rc = True
    for server in servers:
        ret, _, _ = tc.run(server, "cat /proc/mounts | grep '/gluster/lock'")
        if ret != 0:
            tc.logger.error("/gluster/lock mount does not exist on %s"
                            % server)
            _rc = False

    return _rc


def check_if_ctdb_file_exists(servers=None):
    '''Checks if /etc/sysconfig/ctdb file exists
    Kwargs:
        servers (list): The list of servers on which we need
            to check if /etc/sysconfig/ctdb file exists.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        check_if_ctdb_file_exists()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    file_path = "/etc/sysconfig/ctdb"
    if not isinstance(servers, list):
        servers = [servers]
    _rc = True
    for server in servers:
        conn = tc.get_connection(server, "root")
        if not conn.modules.os.path.isfile(file_path):
            tc.logger.error("%s file not found on %s" % (file_path, server))
            _rc = False
        conn.close()

    return _rc


def create_ctdb_public_addresses(servers=None):
    '''Creates the /etc/ctdb/public_addresses file.
       This file will have a list of vips, routing-prefix and interface,
       in the following format - vip/routing-prefix interface.
    Kwargs:
        servers (list): The list of servers on which we need
            to create the /etc/ctdb/public_addresses file.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        create_ctdb_public_addresses()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    ctdb_vips = (tc.global_config['gluster']['cluster_config']
                 ['smb']['ctdb_vips'])
    if not isinstance(servers, list):
        servers = [servers]
    _rc = True
    data_to_write = ""
    file_path = "/etc/ctdb/public_addresses"
    for each_vip in ctdb_vips:
        data_to_write = (data_to_write + each_vip['vip'] + "/" +
                         str(each_vip['routing_prefix']) + " " +
                         each_vip['interface'] + "\n")
    for server in servers:
        conn = tc.get_connection(server, "root")
        try:
            FH = conn.builtin.open(file_path, "w")
            FH.write(data_to_write)
        except IOError as e:
            tc.logger.error(e)
            _rc = False
        finally:
            FH.close()

    return _rc


def start_ctdb_service(servers=None):
    '''Starts the CTDB service
    Kwargs:
        servers (list): The list of servers on which we need
            to start the CTDB service.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        start_ctdb_service()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    if not isinstance(servers, list):
        servers = [servers]
    _rc = True
    for server in servers:
        ret, _, _ = tc.run(server, "service ctdb start")
        if ret != 0:
            tc.logger.error("failed to start the ctdb service on %s" % server)
            _rc = False

    return _rc


def verify_ctdb_status(mnode=None):
    '''Verifies the ctdb status.
       For each server ip mentioned in /etc/ctdb/nodes file,
       it checks if the ctdb status is OK for it.
    Kwargs:
        mnode (str): Node on which the command has
            to be executed. Default value is ctdb_servers[0].
    Returns:
        bool: True if successful, False otherwise
    Example:
        verify_ctdb_status()
    '''
    if mnode is None:
        mnode = (tc.global_config['gluster']['cluster_config']
                 ['smb']['ctdb_servers'][0]['host'])
    _rc = True
    ret, out, _ = tc.run(mnode, "cat /etc/ctdb/nodes")
    if ret != 0:
        tc.logger.error("failed to get the details of /etc/ctdb/nodes file")
        return False
    servers_list = out.strip().split("\n")
    ret, out, _ = tc.run(mnode, "ctdb status | grep pnn")
    if ret != 0:
        tc.logger.error("failed to get the details of ctdb status")
        return False
    status_list = out.strip().split("\n")
    for status in status_list:
        for server in servers_list:
            if server in status:
                if "OK" in status:
                    tc.logger.info("ctdb status for %s is OK" % server)
                else:
                    tc.logger.error("ctdb status for %s is not OK" % server)
                    _rc = False

    return _rc


def ctdb_gluster_setup(mnode=None, servers=None, meta_volname=None):
    '''Setup CTDB on gluster setup
    Kwargs:
        mnode (str): Node on which the command has
            to be executed. Default value is ctdb_servers[0]
        servers (list): The list of servers on which we need
            the CTDB setup.
            Defaults to ctdb_servers as specified in the config file.
        meta_volname (str) : Name for the ctdb meta volume.
            Dafault ctdb meta volume name is "ctdb".
    Returns:
        bool: True if successful, False otherwise
    Example:
        ctdb_gluster_setup()
    '''
    if mnode is None:
        mnode = (tc.global_config['gluster']['cluster_config']
                 ['smb']['ctdb_servers'][0]['host'])
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    if not isinstance(servers, list):
        servers = [servers]
    no_of_ctdb_servers = len(servers)
    if meta_volname is None:
        meta_volname = "ctdb"

    # 1. firewall setting for ctdb setup
    ret = ctdb_firewall_settings(servers[:])
    if ret:
        tc.logger.info("firewall settings successfull for ctdb setup")
    else:
        tc.logger.error("firewall settings failed for ctdb setup")
        return False

    # 2. peer probe
    ret = peer_probe_servers(servers[:], mnode=mnode)
    if not ret:
        return False

    # 3. create ctdb meta volume
    ret = create_ctdb_meta_volume(mnode, servers[:], meta_volname)
    if ret:
        tc.logger.info("successfully created ctdb meta volume")
    else:
        tc.logger.error("failed to create ctdb meta volume")
        return False
    tc.run(mnode, "gluster v info %s" % meta_volname)

    # 4. update the ctdb hook scripts
    ret = update_hook_scripts(servers[:])
    if ret:
        tc.logger.info("successfully updated the hook scripts on all servers")
    else:
        tc.logger.error("failed to update the hook scripts on "
                        "one or more servers")
        return False

    # 5. update the smb.conf file
    ret = update_smb_conf(servers[:])
    if ret:
        tc.logger.info("successfully updated the smb.conf file on all servers")
    else:
        tc.logger.error("failed to update the smb.conf file on "
                        "one or more servers")
        return False

    # 6a. start the meta volume
    ret = start_volume(meta_volname, mnode)
    if ret:
        tc.logger.info("successfully started the meta volume")
        tc.run(mnode, "gluster v status ctdb")
    else:
        tc.logger.error("failed to start the meta volume")
        return False
    time.sleep(20)
    # 6.b check if /gluster/lock mount exists on all servers
    ret = check_if_gluster_lock_mount_exists(servers[:])
    if ret:
        tc.logger.info("/gluster/lock mount exists on all servers")
    else:
        return False

    # 7. check if /etc/sysconfig/ctdb file exists
    ret = check_if_ctdb_file_exists(servers[:])
    if ret:
        tc.logger.info("/etc/sysconfig/ctdb file exists on all servers")
    else:
        return False

    # 8. create /etc/ctdb/nodes file
    ret = create_ctdb_nodes_file(servers[:])
    if ret:
        tc.logger.info("successfully created /etc/ctdb/nodes file on "
                       "all servers")
    else:
        tc.logger.error("failed to create /etc/ctdb/nodes file on "
                        "one or more servers")
        return False

    # 9. create /etc/ctdb/public_addresses file
    ret = create_ctdb_public_addresses(servers[:])
    if ret:
        tc.logger.info("successfully created /etc/ctdb/public_addresses file "
                       "on all servers")
    else:
        tc.logger.error("failed to create /etc/ctdb/public_addresses file "
                        "on one or more servers")
        return False

    # 10. start the ctdb service
    ret = start_ctdb_service(servers[:])
    if ret:
        tc.logger.info("successfully started ctdb service on all servers")
    else:
        return False
    time.sleep(360)

    # 11. verify the ctdb status
    ret = verify_ctdb_status(mnode)
    if ret:
        tc.logger.info("ctdb status is correct")
    else:
        tc.logger.error("ctdb status is incorrect")
        return False

    return True


def stop_ctdb_service(servers=None):
    '''Stops the CTDB service
    Kwargs:
        servers (list): The list of servers on which we need
            to stop the CTDB service.
            Defaults to ctdb_servers as specified in the config file.
    Returns:
        bool: True if successful, False otherwise
    Example:
        stop_ctdb_service()
    '''
    if servers is None:
        servers = (tc.global_config['gluster']['cluster_config']
                   ['smb']['ctdb_servers'])
        server_host_list = []
        for server in servers:
            server_host_list.append(server['host'])
        servers = server_host_list
    if not isinstance(servers, list):
        servers = [servers]
    _rc = True
    for server in servers:
        ret, _, _ = tc.run(server, "service ctdb stop")
        if ret != 0:
            tc.logger.error("failed to stop the ctdb service on %s" % server)
            _rc = False

    return _rc
