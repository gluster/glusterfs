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
import os, fcntl, time
from ConfigParser import ConfigParser
from swift.common.utils import TRUE_VALUES
from hashlib import md5
from swift.plugins.fs_utils import mkdirs


#
# Read the fs.conf file once at startup (module load)
#
_fs_conf = ConfigParser()
MOUNT_PATH = '/mnt/gluster-object'
AUTH_ACCOUNT = 'auth'
MOUNT_IP = 'localhost'
REMOTE_CLUSTER = False
OBJECT_ONLY = False
if _fs_conf.read(os.path.join('/etc/swift', 'fs.conf')):
    try:
        MOUNT_PATH = _fs_conf.get('DEFAULT', 'mount_path', '/mnt/gluster-object')
    except (NoSectionError, NoOptionError):
        pass
    try:
        AUTH_ACCOUNT = _fs_conf.get('DEFAULT', 'auth_account', 'auth')
    except (NoSectionError, NoOptionError):
        pass
    try:
        MOUNT_IP = _fs_conf.get('DEFAULT', 'mount_ip', 'localhost')
    except (NoSectionError, NoOptionError):
        pass
    try:
        REMOTE_CLUSTER = _fs_conf.get('DEFAULT', 'remote_cluster', False) in TRUE_VALUES
    except (NoSectionError, NoOptionError):
        pass
    try:
        OBJECT_ONLY = _fs_conf.get('DEFAULT', 'object_only', "no") in TRUE_VALUES
    except (NoSectionError, NoOptionError):
        pass
NAME = 'glusterfs'


def strip_obj_storage_path(path, mp=MOUNT_PATH):
    """
    strip the mount path off, also stripping the leading and trailing slashes
    """
    return path.replace(mp, '').strip(os.path.sep)

def _busy_wait(full_mount_path):
    # Iterate for definite number of time over a given
    # interval for successful mount
    for i in range(0, 5):
        if os.path.ismount(os.path.join(full_mount_path)):
            return True
        time.sleep(2)
    return False

def mount(account):
    global mount_path, mount_ip

    full_mount_path = os.path.join(mount_path, account)
    export = get_export_from_account_id(account)

    pid_dir  = "/var/lib/glusterd/vols/%s/run/" % export
    pid_file = os.path.join(pid_dir, 'swift.pid');

    if not os.path.exists(pid_dir):
        mkdirs(pid_dir)

    fd = os.open(pid_file, os.O_CREAT|os.O_RDWR)
    with os.fdopen(fd, 'r+b') as f:
        try:
            fcntl.lockf(f, fcntl.LOCK_EX|fcntl.LOCK_NB)
        except:
            ex = sys.exc_info()[1]
            if isinstance(ex, IOError) and ex.errno in (EACCES, EAGAIN):
            # This means that some other process is mounting the
            # filesystem, so wait for the mount process to complete
                return _busy_wait(full_mount_path)

        mnt_cmd = 'mount -t glusterfs %s:%s %s' % (mount_ip, export, \
                                                   full_mount_path)
        if os.system(mnt_cmd) or not _busy_wait(full_mount_path):
            raise Exception('Mount failed %s: %s' % (NAME, mnt_cmd))
    return True

def unmount(full_mount_path):
    umnt_cmd = 'umount %s 2>> /dev/null' % full_mount_path
    if os.system(umnt_cmd):
        logging.error('Unable to unmount %s %s' % (full_mount_path, NAME))

def get_export_list():
    global mount_ip

    if REMOTE_CLUSTER:
        cmnd = 'ssh %s gluster volume info' % mount_ip
    else:
        cmnd = 'gluster volume info'

    if os.system(cmnd + ' >> /dev/null'):
        if REMOTE_CLUSTER:
            raise Exception('Getting volume info failed %s, make sure to have \
                            passwordless ssh on %s', NAME, mount_ip)
        else:
            raise Exception('Getting volume failed %s', NAME)

    export_list = []
    fp = os.popen(cmnd)
    while True:
        item = fp.readline()
        if not item:
            break
        item = item.strip('\n').strip(' ')
        if item.lower().startswith('volume name:'):
            export_list.append(item.split(':')[1].strip(' '))

    return export_list

def get_export_from_account_id(account):
    if not account:
        raise ValueError('No account given')

    for export in get_export_list():
        if account == 'AUTH_' + export:
            return export

    raise Exception('No export found %s %s' % (account, NAME))
