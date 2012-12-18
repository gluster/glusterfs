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

import os
import errno
import random
from hashlib import md5
from eventlet import tpool
from contextlib import contextmanager
from swift.common.utils import normalize_timestamp, renamer
from swift.common.exceptions import DiskFileNotExist
from gluster.swift.common.utils import mkdirs, rmdirs, validate_object, \
     create_object_metadata,  do_open, do_close, do_unlink, do_chown, \
     do_stat, do_listdir, read_metadata, write_metadata
from gluster.swift.common.utils import X_CONTENT_TYPE, X_CONTENT_LENGTH, \
     X_TIMESTAMP, X_PUT_TIMESTAMP, X_TYPE, X_ETAG, X_OBJECTS_COUNT, \
     X_BYTES_USED, X_OBJECT_TYPE, FILE, DIR, MARKER_DIR, OBJECT, DIR_TYPE, \
     FILE_TYPE, DEFAULT_UID, DEFAULT_GID

import logging
from swift.obj.server import DiskFile


DEFAULT_DISK_CHUNK_SIZE = 65536
# keep these lower-case
DISALLOWED_HEADERS = set('content-length content-type deleted etag'.split())


class AlreadyExistsAsDir(Exception):
    pass


def _adjust_metadata(metadata):
    # Fix up the metadata to ensure it has a proper value for the
    # Content-Type metadata, as well as an X_TYPE and X_OBJECT_TYPE
    # metadata values.
    content_type = metadata['Content-Type']
    if not content_type:
        # FIXME: How can this be that our caller supplied us with metadata
        # that has a content type that evaluates to False?
        #
        # FIXME: If the file exists, we would already know it is a
        # directory. So why are we assuming it is a file object?
        metadata['Content-Type'] = FILE_TYPE
        x_object_type = FILE
    else:
        x_object_type = MARKER_DIR if content_type.lower() == DIR_TYPE else FILE
    metadata[X_TYPE] = OBJECT
    metadata[X_OBJECT_TYPE] = x_object_type
    return metadata


class Gluster_DiskFile(DiskFile):
    """
    Manage object files on disk.

    :param path: path to devices on the node/mount path for UFO.
    :param device: device name/account_name for UFO.
    :param partition: partition on the device the object lives in
    :param account: account name for the object
    :param container: container name for the object
    :param obj: object name for the object
    :param logger: logger object for writing out log file messages
    :param keep_data_fp: if True, don't close the fp, otherwise close it
    :param disk_chunk_Size: size of chunks on file reads
    :param uid: user ID disk object should assume (file or directory)
    :param gid: group ID disk object should assume (file or directory)
    """

    def __init__(self, path, device, partition, account, container, obj,
                 logger, keep_data_fp=False,
                 disk_chunk_size=DEFAULT_DISK_CHUNK_SIZE,
                 uid=DEFAULT_UID, gid=DEFAULT_GID, iter_hook=None):
        self.disk_chunk_size = disk_chunk_size
        self.iter_hook = iter_hook
        # Don't support obj_name ending/begining with '/', like /a, a/, /a/b/,
        # etc.
        obj = obj.strip(os.path.sep)
        if os.path.sep in obj:
            self._obj_path, self._obj = os.path.split(obj)
        else:
            self._obj_path = ''
            self._obj = obj

        if self._obj_path:
            self.name = os.path.join(container, self._obj_path)
        else:
            self.name = container
        # Absolute path for object directory.
        self.datadir = os.path.join(path, device, self.name)
        self.device_path = os.path.join(path, device)
        self._container_path = os.path.join(path, device, container)
        self._is_dir = False
        self.tmppath = None
        self.logger = logger
        self.metadata = {}
        self.meta_file = None
        self.fp = None
        self.iter_etag = None
        self.started_at_0 = False
        self.read_to_eof = False
        self.quarantined_dir = None
        self.keep_cache = False
        self.uid = int(uid)
        self.gid = int(gid)

        # Don't store a value for data_file until we know it exists.
        self.data_file = None
        data_file = os.path.join(self.datadir, self._obj)
        if not os.path.exists(data_file):
            return

        self.data_file = os.path.join(data_file)
        self.metadata = read_metadata(data_file)
        if not self.metadata:
            create_object_metadata(data_file)
            self.metadata = read_metadata(data_file)

        if not validate_object(self.metadata):
            create_object_metadata(data_file)
            self.metadata = read_metadata(data_file)

        self.filter_metadata()

        if os.path.isdir(data_file):
            self._is_dir = True
        else:
            if keep_data_fp:
                # The caller has an assumption that the "fp" field of this
                # object is an file object if keep_data_fp is set. However,
                # this implementation of the DiskFile object does not need to
                # open the file for internal operations. So if the caller
                # requests it, we'll just open the file for them.
                self.fp = do_open(data_file, 'rb')

    def close(self, verify_file=True):
        """
        Close the file. Will handle quarantining file if necessary.

        :param verify_file: Defaults to True. If false, will not check
                            file to see if it needs quarantining.
        """
        #Marker directory
        if self._is_dir:
            return
        if self.fp:
            do_close(self.fp)
            self.fp = None

    def is_deleted(self):
        """
        Check if the file is deleted.

        :returns: True if the file doesn't exist or has been flagged as
                  deleted.
        """
        return not self.data_file

    def _create_dir_object(self, dir_path):
        #TODO: if object already exists???
        if os.path.exists(dir_path) and not os.path.isdir(dir_path):
            self.logger.error("Deleting file %s", dir_path)
            do_unlink(dir_path)
        #If dir aleady exist just override metadata.
        mkdirs(dir_path)
        do_chown(dir_path, self.uid, self.gid)
        create_object_metadata(dir_path)

    def put_metadata(self, metadata, tombstone=False):
        """
        Short hand for putting metadata to .meta and .ts files.

        :param metadata: dictionary of metadata to be written
        :param tombstone: whether or not we are writing a tombstone
        """
        if tombstone:
            # We don't write tombstone files. So do nothing.
            return
        assert self.data_file is not None, "put_metadata: no file to put metadata into"
        metadata = _adjust_metadata(metadata)
        write_metadata(self.data_file, metadata)
        self.metadata = metadata
        self.filter_metadata()

    def put(self, fd, metadata, extension='.data'):
        """
        Finalize writing the file on disk, and renames it from the temp file to
        the real location.  This should be called after the data has been
        written to the temp file.

        :param fd: file descriptor of the temp file
        :param metadata: dictionary of metadata to be written
        :param extension: extension to be used when making the file
        """
        # Our caller will use '.data' here; we just ignore it since we map the
        # URL directly to the file system.
        extension = ''

        metadata = _adjust_metadata(metadata)

        if metadata[X_OBJECT_TYPE] == MARKER_DIR:
            if not self.data_file:
                self.data_file = os.path.join(self.datadir, self._obj)
                self._create_dir_object(self.data_file)
            self.put_metadata(metadata)
            return

        # Check if directory already exists.
        if self._is_dir:
            # FIXME: How can we have a directory and it not be marked as a
            # MARKER_DIR (see above)?
            msg = 'File object exists as a directory: %s' % self.data_file
            raise AlreadyExistsAsDir(msg)

        timestamp = normalize_timestamp(metadata[X_TIMESTAMP])
        write_metadata(self.tmppath, metadata)
        if X_CONTENT_LENGTH in metadata:
            self.drop_cache(fd, 0, int(metadata[X_CONTENT_LENGTH]))
        tpool.execute(os.fsync, fd)
        if self._obj_path:
            dir_objs = self._obj_path.split('/')
            assert len(dir_objs) >= 1
            tmp_path = self._container_path
            for dir_name in dir_objs:
                tmp_path = os.path.join(tmp_path, dir_name)
                self._create_dir_object(tmp_path)

        newpath = os.path.join(self.datadir, self._obj)
        renamer(self.tmppath, newpath)
        do_chown(newpath, self.uid, self.gid)
        self.metadata = metadata
        self.data_file = newpath
        self.filter_metadata()
        return

    def unlinkold(self, timestamp):
        """
        Remove any older versions of the object file.  Any file that has an
        older timestamp than timestamp will be deleted.

        :param timestamp: timestamp to compare with each file
        """
        if not self.metadata or self.metadata['X-Timestamp'] >= timestamp:
            return

        assert self.data_file, \
            "Have metadata, %r, but no data_file" % self.metadata

        if self._is_dir:
            # Marker directory object
            if not rmdirs(self.data_file):
                logging.error('Unable to delete dir object: %s', self.data_file)
                return
        else:
            # File object
            do_unlink(self.data_file)

        self.metadata = {}
        self.data_file = None

    def get_data_file_size(self):
        """
        Returns the os.path.getsize for the file.  Raises an exception if this
        file does not match the Content-Length stored in the metadata. Or if
        self.data_file does not exist.

        :returns: file size as an int
        :raises DiskFileError: on file size mismatch.
        :raises DiskFileNotExist: on file not existing (including deleted)
        """
        #Marker directory.
        if self._is_dir:
            return 0
        try:
            file_size = 0
            if self.data_file:
                file_size = os.path.getsize(self.data_file)
                if  X_CONTENT_LENGTH in self.metadata:
                    metadata_size = int(self.metadata[X_CONTENT_LENGTH])
                    if file_size != metadata_size:
                        self.metadata[X_CONTENT_LENGTH] = file_size
                        write_metadata(self.data_file, self.metadata)

                return file_size
        except OSError as err:
            if err.errno != errno.ENOENT:
                raise
        raise DiskFileNotExist('Data File does not exist.')

    def filter_metadata(self):
        if X_TYPE in self.metadata:
            self.metadata.pop(X_TYPE)
        if X_OBJECT_TYPE in self.metadata:
            self.metadata.pop(X_OBJECT_TYPE)

    @contextmanager
    def mkstemp(self):
        """Contextmanager to make a temporary file."""

        # Creating intermidiate directories and corresponding metadata.
        # For optimization, check if the subdirectory already exists,
        # if exists, then it means that it also has its metadata.
        # Not checking for container, since the container should already
        # exist for the call to come here.
        if not os.path.exists(self.datadir):
            path = self._container_path
            subdir_list = self._obj_path.split(os.path.sep)
            for i in range(len(subdir_list)):
                path = os.path.join(path, subdir_list[i]);
                if not os.path.exists(path):
                    self._create_dir_object(path)

        tmpfile = '.' + self._obj + '.' + md5(self._obj + \
                  str(random.random())).hexdigest()

        self.tmppath = os.path.join(self.datadir, tmpfile)
        fd = os.open(self.tmppath, os.O_RDWR | os.O_CREAT | os.O_EXCL)
        try:
            yield fd
        finally:
            try:
                os.close(fd)
            except OSError:
                pass
            tmppath, self.tmppath = self.tmppath, None
            try:
                os.unlink(tmppath)
            except OSError:
                pass
