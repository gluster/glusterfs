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

def do_mkdir(path):
    try:
        os.mkdir(path)
    except Exception, err:
        logging.exception("Mkdir failed on %s err: %s", path, str(err))
        if err.errno != errno.EEXIST:
            raise
    return True

def do_makedirs(path):
    try:
        os.makedirs(path)
    except Exception, err:
        logging.exception("Makedirs failed on %s err: %s", path, str(err))
        if err.errno != errno.EEXIST:
            raise
    return True

def do_listdir(path):
    try:
        buf = os.listdir(path)
    except Exception, err:
        logging.exception("Listdir failed on %s err: %s", path, str(err))
        raise
    return buf

def do_chown(path, uid, gid):
    try:
        os.chown(path, uid, gid)
    except Exception, err:
        logging.exception("Chown failed on %s err: %s", path, str(err))
        raise
    return True

def do_stat(path):
    try:
        #Check for fd.
        if isinstance(path, int):
            buf = os.fstat(path)
        else:
            buf = os.stat(path)
    except Exception, err:
        logging.exception("Stat failed on %s err: %s", path, str(err))
        raise

    return buf

def do_open(path, mode):
    try:
        fd = open(path, mode)
    except Exception, err:
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
    except Exception, err:
        logging.exception("Close failed on %s err: %s", fd, str(err))
        raise
    return True

def do_unlink(path, log = True):
    try:
        os.unlink(path)
    except Exception, err:
        if log:
            logging.exception("Unlink failed on %s err: %s", path, str(err))
        if err.errno != errno.ENOENT:
            raise
    return True

def do_rmdir(path):
    try:
        os.rmdir(path)
    except Exception, err:
        logging.exception("Rmdir failed on %s err: %s", path, str(err))
        if err.errno != errno.ENOENT:
            raise
    return True

def do_rename(old_path, new_path):
    try:
        os.rename(old_path, new_path)
    except Exception, err:
        logging.exception("Rename failed on %s to %s  err: %s", old_path, new_path, \
                          str(err))
        raise
    return True

def mkdirs(path):
    """
    Ensures the path is a directory or makes it if not. Errors if the path
    exists but is a file or on permissions failure.

    :param path: path to create
    """
    if not os.path.isdir(path):
        try:
            do_makedirs(path)
        except OSError, err:
            #TODO: check, isdir will fail if mounted and volume stopped.
            #if err.errno != errno.EEXIST or not os.path.isdir(path)
            if err.errno != errno.EEXIST:
                raise

def dir_empty(path):
    """
    Return true if directory/container is empty.
    :param path: Directory path.
    :returns: True/False.
    """
    if os.path.isdir(path):
        try:
            files = do_listdir(path)
        except Exception, err:
            logging.exception("listdir failed on %s err: %s", path, str(err))
            raise
        if not files:
            return True
        else:
            return False
    else:
        if not os.path.exists(path):
            return True

def rmdirs(path):
    if os.path.isdir(path) and dir_empty(path):
        do_rmdir(path)
    else:
        logging.error("rmdirs failed dir may not be empty or not valid dir")
        return False
