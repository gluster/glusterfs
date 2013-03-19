# Copyright (c) 2012 Red Hat, Inc.
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
from ConfigParser import ConfigParser, NoSectionError, NoOptionError
from swift.common.utils import TRUE_VALUES, search_tree
from gluster.swift.common.fs_utils import mkdirs

#
# Read the fs.conf file once at startup (module load)
#
_fs_conf = ConfigParser()
MOUNT_IP = 'localhost'
OBJECT_ONLY = False
RUN_DIR='/var/run/swift'
SWIFT_DIR = '/etc/swift'
if _fs_conf.read(os.path.join('/etc/swift', 'fs.conf')):
    try:
        MOUNT_IP = _fs_conf.get('DEFAULT', 'mount_ip', 'localhost')
    except (NoSectionError, NoOptionError):
        pass
    try:
        OBJECT_ONLY = _fs_conf.get('DEFAULT', 'object_only', "no") in TRUE_VALUES
    except (NoSectionError, NoOptionError):
        pass
    try:
        RUN_DIR = _fs_conf.get('DEFAULT', 'run_dir', '/var/run/swift')
    except (NoSectionError, NoOptionError):
        pass

NAME = 'glusterfs'


def _busy_wait(full_mount_path):
    # Iterate for definite number of time over a given
    # interval for successful mount
    for i in range(0, 5):
        if os.path.ismount(os.path.join(full_mount_path)):
            return True
        time.sleep(2)
    logging.error('Busy wait for mount timed out for mount %s', full_mount_path)
    return False

def mount(root, drive):
    # FIXME: Possible thundering herd problem here

    el = _get_export_list()
    for export in el:
        if drive == export:
            break
    else:
        logging.error('No export found in %r matching drive, %s', el, drive)
        return False

    # NOTE: root is typically the default value of /mnt/gluster-object
    full_mount_path = os.path.join(root, drive)
    if not os.path.isdir(full_mount_path):
        mkdirs(full_mount_path)

    lck_file = os.path.join(RUN_DIR, '%s.lock' %drive);

    if not os.path.exists(RUN_DIR):
        mkdirs(RUN_DIR)

    fd = os.open(lck_file, os.O_CREAT|os.O_RDWR)
    with os.fdopen(fd, 'r+b') as f:
        try:
            fcntl.lockf(f, fcntl.LOCK_EX|fcntl.LOCK_NB)
        except:
            ex = sys.exc_info()[1]
            if isinstance(ex, IOError) and ex.errno in (EACCES, EAGAIN):
                # This means that some other process is mounting the
                # filesystem, so wait for the mount process to complete
                return _busy_wait(full_mount_path)

        mnt_cmd = 'mount -t glusterfs %s:%s %s' % (MOUNT_IP, export, \
                                                   full_mount_path)
        if os.system(mnt_cmd) or not _busy_wait(full_mount_path):
            logging.error('Mount failed %s: %s', NAME, mnt_cmd)
            return False
    return True

def unmount(full_mount_path):
    # FIXME: Possible thundering herd problem here

    umnt_cmd = 'umount %s 2>> /dev/null' % full_mount_path
    if os.system(umnt_cmd):
        logging.error('Unable to unmount %s %s' % (full_mount_path, NAME))

def _get_export_list():
    cmnd = 'gluster --remote-host=%s volume info' % MOUNT_IP

    export_list = []

    if os.system(cmnd + ' >> /dev/null'):
        logging.error('Getting volume info failed for %s', NAME)
    else:
        fp = os.popen(cmnd)
        while True:
            item = fp.readline()
            if not item:
                break
            item = item.strip('\n').strip(' ')
            if item.lower().startswith('volume name:'):
                export_list.append(item.split(':')[1].strip(' '))

    return export_list

def get_mnt_point(vol_name, conf_dir=SWIFT_DIR, conf_file="object-server*"):
    """Read the object-server's configuration file and return
    the device value"""

    mnt_dir = ''
    conf_files = search_tree(conf_dir, conf_file, '.conf')
    if not conf_files:
        raise Exception("Config file not found")

    _conf = ConfigParser()
    if _conf.read(conf_files[0]):
        try:
            mnt_dir = _conf.get('DEFAULT', 'devices', '')
        except (NoSectionError, NoOptionError):
            raise
        return os.path.join(mnt_dir, vol_name)
