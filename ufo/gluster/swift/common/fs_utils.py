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
import os
import errno
import os.path as os_path
from eventlet import tpool
from gluster.swift.common.exceptions import FileOrDirNotFoundError, \
    NotDirectoryError

def do_walk(*args, **kwargs):
    return os.walk(*args, **kwargs)

def do_write(fd, msg):
    try:
        cnt = os.write(fd, msg)
    except OSError as err:
        logging.exception("Write failed, err: %s", str(err))
        raise
    return cnt

def do_mkdir(path):
    try:
        os.mkdir(path)
    except OSError as err:
        if err.errno != errno.EEXIST:
            logging.exception("Mkdir failed on %s err: %s", path, err.strerror)
            raise
    return True

def do_makedirs(path):
    try:
        os.makedirs(path)
    except OSError as err:
        if err.errno != errno.EEXIST:
            logging.exception("Makedirs failed on %s err: %s", path, err.strerror)
            raise
    return True

def do_listdir(path):
    try:
        buf = os.listdir(path)
    except OSError as err:
        logging.exception("Listdir failed on %s err: %s", path, err.strerror)
        raise
    return buf

def do_chown(path, uid, gid):
    try:
        os.chown(path, uid, gid)
    except OSError as err:
        logging.exception("Chown failed on %s err: %s", path, err.strerror)
        raise
    return True

def do_stat(path):
    try:
        #Check for fd.
        if isinstance(path, int):
            buf = os.fstat(path)
        else:
            buf = os.stat(path)
    except OSError as err:
        logging.exception("Stat failed on %s err: %s", path, err.strerror)
        raise
    return buf

def do_open(path, mode):
    if isinstance(mode, int):
        try:
            fd = os.open(path, mode)
        except OSError as err:
            logging.exception("Open failed on %s err: %s", path, str(err))
            raise
    else:
        try:
            fd = open(path, mode)
        except IOError as err:
            logging.exception("Open failed on %s err: %s", path, str(err))
            raise
    return fd

def do_close(fd):
    #fd could be file or int type.
    try:
        if isinstance(fd, int):
            os.close(fd)
        else:
            fd.close()
    except OSError as err:
        logging.exception("Close failed on %s err: %s", fd, err.strerror)
        raise
    return True

def do_unlink(path, log = True):
    try:
        os.unlink(path)
    except OSError as err:
        if err.errno != errno.ENOENT:
            if log:
                logging.exception("Unlink failed on %s err: %s", path, err.strerror)
            raise
    return True

def do_rmdir(path):
    try:
        os.rmdir(path)
    except OSError as err:
        if err.errno != errno.ENOENT:
            logging.exception("Rmdir failed on %s err: %s", path, err.strerror)
            raise
        res = False
    else:
        res = True
    return res

def do_rename(old_path, new_path):
    try:
        os.rename(old_path, new_path)
    except OSError as err:
        logging.exception("Rename failed on %s to %s  err: %s", old_path, new_path, \
                          err.strerror)
        raise
    return True

def mkdirs(path):
    """
    Ensures the path is a directory or makes it if not. Errors if the path
    exists but is a file or on permissions failure.

    :param path: path to create
    """
    if not os.path.isdir(path):
        do_makedirs(path)

def dir_empty(path):
    """
    Return true if directory/container is empty.
    :param path: Directory path.
    :returns: True/False.
    """
    if os.path.isdir(path):
        files = do_listdir(path)
        return not files
    elif not os.path.exists(path):
        raise FileOrDirNotFoundError()
    raise NotDirectoryError()

def rmdirs(path):
    if not os.path.isdir(path):
        return False
    try:
        os.rmdir(path)
    except OSError as err:
        if err.errno != errno.ENOENT:
            logging.error("rmdirs failed on %s, err: %s", path, err.strerror)
            return False
    return True

def do_fsync(fd):
    try:
        tpool.execute(os.fsync, fd)
    except OSError as err:
        logging.exception("fsync failed with err: %s", err.strerror)
        raise
    return True
