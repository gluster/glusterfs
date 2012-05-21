# Copyright (c) 2011 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import logging
import os
from ConfigParser import ConfigParser
from swift.common.utils import TRUE_VALUES
from hashlib import md5

class Glusterfs(object):
    def __init__(self):
        self.name = 'glusterfs'
        self.fs_conf = ConfigParser()
        self.fs_conf.read(os.path.join('/etc/swift', 'fs.conf'))
        self.mount_path = self.fs_conf.get('DEFAULT', 'mount_path', '/mnt/gluster-object')
        self.auth_account = self.fs_conf.get('DEFAULT', 'auth_account', 'auth')
        self.mount_ip = self.fs_conf.get('DEFAULT', 'mount_ip', 'localhost')
        self.remote_cluster = self.fs_conf.get('DEFAULT', 'remote_cluster', False) in TRUE_VALUES

    def mount(self, account):
        export = self.get_export_from_account_id(account)
        mount_path = os.path.join(self.mount_path, account)
        mnt_cmd = 'mount -t glusterfs %s:%s %s' % (self.mount_ip, export, \
                                                   mount_path)
        if os.system(mnt_cmd) or \
        not os.path.exists(os.path.join(mount_path)):
            raise Exception('Mount failed %s: %s' % (self.name, mnt_cmd))
            return False
        return True

    def unmount(self, mount_path):
        umnt_cmd = 'umount %s 2>> /dev/null' % mount_path
        if os.system(umnt_cmd):
            logging.error('Unable to unmount %s %s' % (mount_path, self.name))

    def get_export_list_local(self):
        export_list = []
        cmnd = 'gluster volume info'

        if os.system(cmnd + ' >> /dev/null'):
            raise Exception('Getting volume failed %s', self.name)
            return export_list

        fp = os.popen(cmnd)
        while True:
            item = fp.readline()
            if not item:
                break
            item = item.strip('\n').strip(' ')
            if item.lower().startswith('volume name:'):
                export_list.append(item.split(':')[1].strip(' '))

        return export_list


    def get_export_list_remote(self):
        export_list = []
        cmnd = 'ssh %s gluster volume info' % self.mount_ip

        if os.system(cmnd + ' >> /dev/null'):
            raise Exception('Getting volume info failed %s, make sure to have \
                            passwordless ssh on %s', self.name, self.mount_ip)
            return export_list

        fp = os.popen(cmnd)
        while True:
            item = fp.readline()
            if not item:
                break
            item = item.strip('\n').strip(' ')
            if item.lower().startswith('volume name:'):
                export_list.append(item.split(':')[1].strip(' '))

        return export_list

    def get_export_list(self):
        if self.remote_cluster:
            return self.get_export_list_remote()
        else:
            return self.get_export_list_local()

    def get_export_from_account_id(self, account):
        if not account:
            print 'account is none, returning'
            raise AttributeError

        for export in self.get_export_list():
            if account == 'AUTH_' + export:
                return export

        raise Exception('No export found %s %s' % (account, self.name))
        return None
