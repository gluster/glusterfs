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
import xattr
import random
from hashlib import md5
from eventlet import sleep
import cPickle as pickle
from ConfigParser import ConfigParser, NoSectionError, NoOptionError
from swift.common.utils import normalize_timestamp, TRUE_VALUES
from gluster.swift.common.fs_utils import *
from gluster.swift.common import Glusterfs

X_CONTENT_TYPE = 'Content-Type'
X_CONTENT_LENGTH = 'Content-Length'
X_TIMESTAMP = 'X-Timestamp'
X_PUT_TIMESTAMP = 'X-PUT-Timestamp'
X_TYPE = 'X-Type'
X_ETAG = 'ETag'
X_OBJECTS_COUNT = 'X-Object-Count'
X_BYTES_USED = 'X-Bytes-Used'
X_CONTAINER_COUNT = 'X-Container-Count'
X_OBJECT_TYPE = 'X-Object-Type'
DIR_TYPE = 'application/directory'
ACCOUNT = 'Account'
METADATA_KEY = 'user.swift.metadata'
MAX_XATTR_SIZE = 65536
CONTAINER = 'container'
DIR = 'dir'
MARKER_DIR = 'marker_dir'
TEMP_DIR = 'tmp'
ASYNCDIR = 'async_pending'  # Keep in sync with swift.obj.server.ASYNCDIR
FILE = 'file'
FILE_TYPE = 'application/octet-stream'
OBJECT = 'Object'
OBJECT_TYPE = 'application/octet-stream'
DEFAULT_UID = -1
DEFAULT_GID = -1
PICKLE_PROTOCOL = 2
CHUNK_SIZE = 65536
MEMCACHE_KEY_PREFIX = 'gluster.swift.'
MEMCACHE_ACCOUNT_DETAILS_KEY_PREFIX = MEMCACHE_KEY_PREFIX + 'account.details.'
MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX = MEMCACHE_KEY_PREFIX + 'container.details.'

def read_metadata(path):
    """
    Helper function to read the pickled metadata from a File/Directory.

    :param path: File/Directory to read metadata from.

    :returns: dictionary of metadata
    """
    metadata = None
    metadata_s = ''
    key = 0
    while metadata is None:
        try:
            metadata_s += xattr.getxattr(path, '%s%s' % (METADATA_KEY, (key or '')))
        except IOError as err:
            if err.errno == errno.ENODATA:
                if key > 0:
                    # No errors reading the xattr keys, but since we have not
                    # been able to find enough chunks to get a successful
                    # unpickle operation, we consider the metadata lost, and
                    # drop the existing data so that the internal state can be
                    # recreated.
                    clean_metadata(path)
                # We either could not find any metadata key, or we could find
                # some keys, but were not successful in performing the
                # unpickling (missing keys perhaps)? Either way, just report
                # to the caller we have no metadata.
                metadata = {}
            else:
                logging.exception("xattr.getxattr failed on %s key %s err: %s",
                                  path, key, str(err))
                # Note that we don't touch the keys on errors fetching the
                # data since it could be a transient state.
                raise
        else:
            try:
                # If this key provides all or the remaining part of the pickle
                # data, we don't need to keep searching for more keys. This
                # means if we only need to store data in N xattr key/value
                # pair, we only need to invoke xattr get N times. With large
                # keys sizes we are shooting for N = 1.
                metadata = pickle.loads(metadata_s)
                assert isinstance(metadata, dict)
            except EOFError, pickle.UnpicklingError:
                # We still are not able recognize this existing data collected
                # as a pickled object. Make sure we loop around to try to get
                # more from another xattr key.
                metadata = None
                key += 1
    return metadata

def write_metadata(path, metadata):
    """
    Helper function to write pickled metadata for a File/Directory.

    :param path: File/Directory path to write the metadata
    :param metadata: dictionary to metadata write
    """
    assert isinstance(metadata, dict)
    metastr = pickle.dumps(metadata, PICKLE_PROTOCOL)
    key = 0
    while metastr:
        try:
            xattr.setxattr(path, '%s%s' % (METADATA_KEY, key or ''), metastr[:MAX_XATTR_SIZE])
        except IOError as err:
            logging.exception("setxattr failed on %s key %s err: %s", path, key, str(err))
            raise
        metastr = metastr[MAX_XATTR_SIZE:]
        key += 1

def clean_metadata(path):
    key = 0
    while True:
        try:
            xattr.removexattr(path, '%s%s' % (METADATA_KEY, (key or '')))
        except IOError as err:
            if err.errno == errno.ENODATA:
                break
            raise
        key += 1

def check_user_xattr(path):
    if not os_path.exists(path):
        return False
    try:
        xattr.setxattr(path, 'user.test.key1', 'value1')
    except IOError as err:
        logging.exception("check_user_xattr: set failed on %s err: %s", path, str(err))
        raise
    try:
        xattr.removexattr(path, 'user.test.key1')
    except IOError as err:
        logging.exception("check_user_xattr: remove failed on %s err: %s", path, str(err))
        #Remove xattr may fail in case of concurrent remove.
    return True

def validate_container(metadata):
    if not metadata:
        logging.warn('validate_container: No metadata')
        return False

    if X_TYPE not in metadata.keys() or \
       X_TIMESTAMP not in metadata.keys() or \
       X_PUT_TIMESTAMP not in metadata.keys() or \
       X_OBJECTS_COUNT not in metadata.keys() or \
       X_BYTES_USED not in metadata.keys():
        #logging.warn('validate_container: Metadata missing entries: %s' % metadata)
        return False

    (value, timestamp) = metadata[X_TYPE]
    if value == CONTAINER:
        return True

    logging.warn('validate_container: metadata type is not CONTAINER (%r)' % (value,))
    return False

def validate_account(metadata):
    if not metadata:
        logging.warn('validate_account: No metadata')
        return False

    if X_TYPE not in metadata.keys() or \
       X_TIMESTAMP not in metadata.keys() or \
       X_PUT_TIMESTAMP not in metadata.keys() or \
       X_OBJECTS_COUNT not in metadata.keys() or \
       X_BYTES_USED not in metadata.keys() or \
       X_CONTAINER_COUNT not in metadata.keys():
        #logging.warn('validate_account: Metadata missing entries: %s' % metadata)
        return False

    (value, timestamp) = metadata[X_TYPE]
    if value == ACCOUNT:
        return True

    logging.warn('validate_account: metadata type is not ACCOUNT (%r)' % (value,))
    return False

def validate_object(metadata):
    if not metadata:
        logging.warn('validate_object: No metadata')
        return False

    if X_TIMESTAMP not in metadata.keys() or \
       X_CONTENT_TYPE not in metadata.keys() or \
       X_ETAG not in metadata.keys() or \
       X_CONTENT_LENGTH not in metadata.keys() or \
       X_TYPE not in metadata.keys() or \
       X_OBJECT_TYPE not in metadata.keys():
        #logging.warn('validate_object: Metadata missing entries: %s' % metadata)
        return False

    if metadata[X_TYPE] == OBJECT:
        return True

    logging.warn('validate_object: metadata type is not OBJECT (%r)' % (metadata[X_TYPE],))
    return False

def is_marker(metadata):
    if not metadata:
        logging.warn('is_marker: No metadata')
        return False

    if X_OBJECT_TYPE not in metadata.keys():
        logging.warn('is_marker: X_OBJECT_TYPE missing from metadata: %s' % metadata)
        return False

    if metadata[X_OBJECT_TYPE] == MARKER_DIR:
        return True
    else:
        return False

def _update_list(path, cont_path, src_list, reg_file=True, object_count=0,
                 bytes_used=0, obj_list=[]):
    # strip the prefix off, also stripping the leading and trailing slashes
    obj_path = path.replace(cont_path, '').strip(os.path.sep)

    for obj_name in src_list:
        if obj_path:
            obj_list.append(os.path.join(obj_path, obj_name))
        else:
            obj_list.append(obj_name)

        object_count += 1

        if Glusterfs._do_getsize and reg_file:
            bytes_used += os_path.getsize(os.path.join(path, obj_name))
            sleep()

    return object_count, bytes_used

def update_list(path, cont_path, dirs=[], files=[], object_count=0,
                bytes_used=0, obj_list=[]):
    if files:
        object_count, bytes_used = _update_list(path, cont_path, files, True,
                                                object_count, bytes_used,
                                                obj_list)
    if dirs:
        object_count, bytes_used = _update_list(path, cont_path, dirs, False,
                                                object_count, bytes_used,
                                                obj_list)
    return object_count, bytes_used


class ContainerDetails(object):
    def __init__(self, bytes_used, object_count, obj_list, dir_list):
        self.bytes_used = bytes_used
        self.object_count = object_count
        self.obj_list = obj_list
        self.dir_list = dir_list


def _get_container_details_from_fs(cont_path):
    """
    get container details by traversing the filesystem
    """
    bytes_used = 0
    object_count = 0
    obj_list = []
    dir_list = []

    if os_path.isdir(cont_path):
        for (path, dirs, files) in do_walk(cont_path):
            object_count, bytes_used = update_list(path, cont_path, dirs, files,
                                                   object_count, bytes_used,
                                                   obj_list)

            dir_list.append((path, do_stat(path).st_mtime))
            sleep()

    return ContainerDetails(bytes_used, object_count, obj_list, dir_list)

def get_container_details(cont_path, memcache=None):
    """
    Return object_list, object_count and bytes_used.
    """
    mkey = ''
    if memcache:
        mkey = MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + cont_path
        cd = memcache.get(mkey)
        if cd:
            if not cd.dir_list:
                cd = None
            else:
                for (path, mtime) in cd.dir_list:
                    if mtime != do_stat(path).st_mtime:
                        cd = None
    else:
        cd = None
    if not cd:
        cd = _get_container_details_from_fs(cont_path)
        if memcache:
            memcache.set(mkey, cd)
    return cd.obj_list, cd.object_count, cd.bytes_used


class AccountDetails(object):
    """ A simple class to store the three pieces of information associated
        with an account:

        1. The last known modification time
        2. The count of containers in the following list
        3. The list of containers
    """
    def __init__(self, mtime, container_count, container_list):
        self.mtime = mtime
        self.container_count = container_count
        self.container_list = container_list


def _get_account_details_from_fs(acc_path, acc_stats):
    container_list = []
    container_count = 0

    if not acc_stats:
        acc_stats = do_stat(acc_path)
    is_dir = (acc_stats.st_mode & 0040000) != 0
    if is_dir:
        for name in do_listdir(acc_path):
            if name.lower() == TEMP_DIR \
                    or name.lower() == ASYNCDIR \
                    or not os_path.isdir(os.path.join(acc_path, name)):
                continue
            container_count += 1
            container_list.append(name)

    return AccountDetails(acc_stats.st_mtime, container_count, container_list)

def get_account_details(acc_path, memcache=None):
    """
    Return container_list and container_count.
    """
    acc_stats = None
    mkey = ''
    if memcache:
        mkey = MEMCACHE_ACCOUNT_DETAILS_KEY_PREFIX + acc_path
        ad = memcache.get(mkey)
        if ad:
            # FIXME: Do we really need to stat the file? If we are object
            # only, then we can track the other Swift HTTP APIs that would
            # modify the account and invalidate the cached entry there. If we
            # are not object only, are we even called on this path?
            acc_stats = do_stat(acc_path)
            if ad.mtime != acc_stats.st_mtime:
                ad = None
    else:
        ad = None
    if not ad:
        ad = _get_account_details_from_fs(acc_path, acc_stats)
        if memcache:
            memcache.set(mkey, ad)
    return ad.container_list, ad.container_count

def _get_etag(path):
    etag = md5()
    with open(path, 'rb') as fp:
        while True:
            chunk = fp.read(CHUNK_SIZE)
            if chunk:
                etag.update(chunk)
            else:
                break
    return etag.hexdigest()

def get_object_metadata(obj_path):
    """
    Return metadata of object.
    """
    try:
        stats = do_stat(obj_path)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise
        metadata = {}
    else:
        is_dir = (stats.st_mode & 0040000) != 0
        metadata = {
            X_TYPE: OBJECT,
            X_TIMESTAMP: normalize_timestamp(stats.st_ctime),
            X_CONTENT_TYPE: DIR_TYPE if is_dir else FILE_TYPE,
            X_OBJECT_TYPE: DIR if is_dir else FILE,
            X_CONTENT_LENGTH: 0 if is_dir else stats.st_size,
            X_ETAG: md5().hexdigest() if is_dir else _get_etag(obj_path),
            }
    return metadata

def _add_timestamp(metadata_i):
    # At this point we have a simple key/value dictionary, turn it into
    # key/(value,timestamp) pairs.
    timestamp = 0
    metadata = {}
    for key, value_i in metadata_i.iteritems():
        if not isinstance(value_i, tuple):
            metadata[key] = (value_i, timestamp)
        else:
            metadata[key] = value_i
    return metadata

def get_container_metadata(cont_path, memcache=None):
    objects = []
    object_count = 0
    bytes_used = 0
    objects, object_count, bytes_used = get_container_details(cont_path, memcache)
    metadata = {X_TYPE: CONTAINER,
                X_TIMESTAMP: normalize_timestamp(os_path.getctime(cont_path)),
                X_PUT_TIMESTAMP: normalize_timestamp(os_path.getmtime(cont_path)),
                X_OBJECTS_COUNT: object_count,
                X_BYTES_USED: bytes_used}
    return _add_timestamp(metadata)

def get_account_metadata(acc_path, memcache=None):
    containers = []
    container_count = 0
    containers, container_count = get_account_details(acc_path, memcache)
    metadata = {X_TYPE: ACCOUNT,
                X_TIMESTAMP: normalize_timestamp(os_path.getctime(acc_path)),
                X_PUT_TIMESTAMP: normalize_timestamp(os_path.getmtime(acc_path)),
                X_OBJECTS_COUNT: 0,
                X_BYTES_USED: 0,
                X_CONTAINER_COUNT: container_count}
    return _add_timestamp(metadata)

def restore_metadata(path, metadata):
    meta_orig = read_metadata(path)
    if meta_orig:
        meta_new = meta_orig.copy()
        meta_new.update(metadata)
    else:
        meta_new = metadata
    if meta_orig != meta_new:
        write_metadata(path, meta_new)
    return meta_new

def create_object_metadata(obj_path):
    metadata = get_object_metadata(obj_path)
    return restore_metadata(obj_path, metadata)

def create_container_metadata(cont_path, memcache=None):
    metadata = get_container_metadata(cont_path, memcache)
    return restore_metadata(cont_path, metadata)

def create_account_metadata(acc_path, memcache=None):
    metadata = get_account_metadata(acc_path, memcache)
    return restore_metadata(acc_path, metadata)

def write_pickle(obj, dest, tmp=None, pickle_protocol=0):
    """
    Ensure that a pickle file gets written to disk.  The file is first written
    to a tmp file location in the destination directory path, ensured it is
    synced to disk, then moved to its final destination name.

    This version takes advantage of Gluster's dot-prefix-dot-suffix naming
    where the a file named ".thefile.name.9a7aasv" is hashed to the same
    Gluster node as "thefile.name". This ensures the renaming of a temp file
    once written does not move it to another Gluster node.

    :param obj: python object to be pickled
    :param dest: path of final destination file
    :param tmp: path to tmp to use, defaults to None (ignored)
    :param pickle_protocol: protocol to pickle the obj with, defaults to 0
    """
    dirname = os.path.dirname(dest)
    basename = os.path.basename(dest)
    tmpname = '.' + basename + '.' + md5(basename + str(random.random())).hexdigest()
    tmppath = os.path.join(dirname, tmpname)
    with open(tmppath, 'wb') as fo:
        pickle.dump(obj, fo, pickle_protocol)
        # TODO: This flush() method call turns into a flush() system call
        # We'll need to wrap this as well, but we would do this by writing
        #a context manager for our own open() method which returns an object
        # in fo which makes the gluster API call.
        fo.flush()
        do_fsync(fo)
    do_rename(tmppath, dest)

# Over-ride Swift's utils.write_pickle with ours
import swift.common.utils
swift.common.utils.write_pickle = write_pickle
