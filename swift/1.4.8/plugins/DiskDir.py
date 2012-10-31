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

import os, errno

from swift.plugins.utils import clean_metadata, dir_empty, rmdirs, mkdirs, \
     validate_account, validate_container, check_valid_account, is_marker, \
     get_container_details, get_account_details, create_container_metadata, \
     create_account_metadata, DEFAULT_GID, DEFAULT_UID, validate_object, \
     create_object_metadata, read_metadata, write_metadata, X_CONTENT_TYPE, \
     X_CONTENT_LENGTH, X_TIMESTAMP, X_PUT_TIMESTAMP, X_TYPE, X_ETAG, \
     X_OBJECTS_COUNT, X_BYTES_USED, X_CONTAINER_COUNT, CONTAINER

from swift.common.constraints import CONTAINER_LISTING_LIMIT, check_mount
from swift.common.utils import normalize_timestamp, TRUE_VALUES


DATADIR = 'containers'

# Create a dummy db_file in /etc/swift
_unittests_enabled = os.getenv('GLUSTER_UNIT_TEST_ENABLED', 'no')
if _unittests_enabled in TRUE_VALUES:
    _tmp_dir = '/tmp/gluster_unit_tests'
    try:
        os.mkdir(_tmp_dir)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise
    _db_file = os.path.join(_tmp_dir, 'db_file.db')
else:
    _db_file = '/etc/swift/db_file.db'
if not os.path.exists(_db_file):
    file(_db_file, 'w+')


def _read_metadata(dd):
    """ Filter read metadata so that it always returns a tuple that includes
        some kind of timestamp. With 1.4.8 of the Swift integration the
        timestamps were not stored. Here we fabricate timestamps for volumes
        where the existing data has no timestamp (that is, stored data is not
        a tuple), allowing us a measure of backward compatibility.

        FIXME: At this time it does not appear that the timestamps on each
        metadata are used for much, so this should not hurt anything.
    """
    metadata_i = read_metadata(dd)
    metadata = {}
    timestamp = 0
    for key, value in metadata_i.iteritems():
        if not isinstance(value, tuple):
            value = (value, timestamp)
        metadata[key] = value
    return metadata


class DiskCommon(object):
    def is_deleted(self):
        return not os.path.exists(self.datadir)

    def filter_prefix(self, objects, prefix):
        """
        Accept sorted list.
        """
        found = 0
        filtered_objs = []
        for object_name in objects:
            if object_name.startswith(prefix):
                filtered_objs.append(object_name)
                found = 1
            else:
                if found:
                    break
        return filtered_objs

    def filter_delimiter(self, objects, delimiter, prefix):
        """
        Accept sorted list.
        Objects should start with prefix.
        """
        filtered_objs=[]
        for object_name in objects:
            tmp_obj = object_name.replace(prefix, '', 1)
            sufix = tmp_obj.split(delimiter, 1)
            new_obj = prefix + sufix[0]
            if new_obj and new_obj not in filtered_objs:
                filtered_objs.append(new_obj)

        return filtered_objs

    def filter_marker(self, objects, marker):
        """
        TODO: We can traverse in reverse order to optimize.
        Accept sorted list.
        """
        filtered_objs=[]
        found = 0
        if objects[-1] < marker:
            return filtered_objs
        for object_name in objects:
            if object_name > marker:
                filtered_objs.append(object_name)

        return filtered_objs

    def filter_end_marker(self, objects, end_marker):
        """
        Accept sorted list.
        """
        filtered_objs=[]
        for object_name in objects:
            if object_name < end_marker:
                filtered_objs.append(object_name)
            else:
                break

        return filtered_objs

    def filter_limit(self, objects, limit):
        filtered_objs=[]
        for i in range(0, limit):
            filtered_objs.append(objects[i])

        return filtered_objs


class DiskDir(DiskCommon):
    """
    Manage object files on disk.

    :param path: path to devices on the node
    :param account: account name for the object
    :param container: container name for the object
    :param logger: account or container server logging object
    :param uid: user ID container object should assume
    :param gid: group ID container object should assume
    """

    def __init__(self, path, account, container, logger,
                 uid=DEFAULT_UID, gid=DEFAULT_GID):
        self.root = path
        if container:
            self.container = container
        else:
            self.container = None
        if self.container:
            self.datadir = os.path.join(path, account, self.container)
        else:
            self.datadir = os.path.join(path, account)
        # Note that the account name has a one-to-one mapping to the gluster
        # mount point, or volume name.
        self.account = account
        if not check_mount(path, account):
            check_valid_account(account)
        assert logger is not None
        self.logger = logger
        self.metadata = {}
        self.uid = int(uid)
        self.gid = int(gid)
        self.db_file = _db_file
        self.dir_exists = os.path.exists(self.datadir)
        if self.dir_exists:
            try:
                self.metadata = _read_metadata(self.datadir)
            except EOFError:
                create_container_metadata(self.datadir)
        else:
            return
        if self.container:
            if not self.metadata:
                create_container_metadata(self.datadir)
                self.metadata = _read_metadata(self.datadir)
            else:
                if not validate_container(self.metadata):
                    create_container_metadata(self.datadir)
                    self.metadata = _read_metadata(self.datadir)
        else:
            if not self.metadata:
                create_account_metadata(self.datadir)
                self.metadata = _read_metadata(self.datadir)
            else:
                if not validate_account(self.metadata):
                    create_account_metadata(self.datadir)
                    self.metadata = _read_metadata(self.datadir)

    def empty(self):
        return dir_empty(self.datadir)

    def delete(self):
        if self.empty():
            #For delete account.
            if os.path.ismount(self.datadir):
                clean_metadata(self.datadir)
            else:
                rmdirs(self.datadir)
            self.dir_exists = False

    def put_metadata(self, metadata):
        """
        Write metadata to directory/container.
        """
        write_metadata(self.datadir, metadata)
        self.metadata = metadata

    def put(self, metadata):
        """
        Create and write metatdata to directory/container.
        :param metadata: Metadata to write.
        """
        if not self.dir_exists:
            mkdirs(self.datadir)

        os.chown(self.datadir, self.uid, self.gid)
        write_metadata(self.datadir, metadata)
        self.metadata = metadata
        self.dir_exists = True

    def put_obj(self, content_length, timestamp):
        ocnt = self.metadata[X_OBJECTS_COUNT][0]
        self.metadata[X_OBJECTS_COUNT] = (int(ocnt) + 1, timestamp)
        self.metadata[X_PUT_TIMESTAMP] = timestamp
        bused = self.metadata[X_BYTES_USED][0]
        self.metadata[X_BYTES_USED] = (int(bused) + int(content_length), timestamp)
        #TODO: define update_metadata instad of writing whole metadata again.
        self.put_metadata(self.metadata)

    def delete_obj(self, content_length):
        ocnt, timestamp = self.metadata[X_OBJECTS_COUNT][0]
        self.metadata[X_OBJECTS_COUNT] = (int(ocnt) - 1, timestamp)
        bused, timestamp = self.metadata[X_BYTES_USED]
        self.metadata[X_BYTES_USED] = (int(bused) - int(content_length), timestamp)
        self.put_metadata(self.metadata)

    def put_container(self, container, put_timestamp, del_timestamp, object_count, bytes_used):
        """
        For account server.
        """
        self.metadata[X_OBJECTS_COUNT] = (0, put_timestamp)
        self.metadata[X_BYTES_USED] = (0, put_timestamp)
        ccnt = self.metadata[X_CONTAINER_COUNT][0]
        self.metadata[X_CONTAINER_COUNT] = (int(ccnt) + 1, put_timestamp)
        self.metadata[X_PUT_TIMESTAMP] = (1, put_timestamp)
        self.put_metadata(self.metadata)

    def delete_container(self, object_count, bytes_used):
        """
        For account server.
        """
        self.metadata[X_OBJECTS_COUNT] = (0, 0)
        self.metadata[X_BYTES_USED] = (0, 0)
        ccnt, timestamp = self.metadata[X_CONTAINER_COUNT]
        self.metadata[X_CONTAINER_COUNT] = (int(ccnt) - 1, timestamp)
        self.put_metadata(self.metadata)

    def unlink(self):
        """
        Remove directory/container if empty.
        """
        if dir_empty(self.datadir):
            rmdirs(self.datadir)

    def list_objects_iter(self, limit, marker, end_marker,
                          prefix, delimiter, path):
        """
        Returns tuple of name, created_at, size, content_type, etag.
        """
        if path:
            prefix = path = path.rstrip('/') + '/'
            delimiter = '/'
        if delimiter and not prefix:
            prefix = ''

        objects, object_count, bytes_used = get_container_details(self.datadir)

        if int(self.metadata[X_OBJECTS_COUNT][0]) != object_count or \
           int(self.metadata[X_BYTES_USED][0]) != bytes_used:
            self.metadata[X_OBJECTS_COUNT] = (object_count, 0)
            self.metadata[X_BYTES_USED] = (bytes_used, 0)
            self.update_container(self.metadata)

        if objects:
            objects.sort()

        if objects and prefix:
            objects = self.filter_prefix(objects, prefix)

        if objects and delimiter:
            objects = self.filter_delimiter(objects, delimiter, prefix)

        if objects and marker:
            objects = self.filter_marker(objects, marker)

        if objects and end_marker:
            objects = self.filter_end_marker(objects, end_marker)

        if objects and limit:
            if len(objects) > limit:
                objects = self.filter_limit(objects, limit)

        container_list = []
        if objects:
            for obj in objects:
                list_item = []
                list_item.append(obj)
                obj_path = os.path.join(self.datadir, obj)
                metadata = read_metadata(obj_path)
                if not metadata or not validate_object(metadata):
                    metadata = create_object_metadata(obj_path)
                if metadata:
                    list_item.append(metadata[X_TIMESTAMP])
                    list_item.append(int(metadata[X_CONTENT_LENGTH]))
                    list_item.append(metadata[X_CONTENT_TYPE])
                    list_item.append(metadata[X_ETAG])
                container_list.append(list_item)

        return container_list

    def update_container(self, metadata):
        cont_path = self.datadir
        write_metadata(cont_path, metadata)
        self.metadata = metadata

    def update_object_count(self):
        objects = []
        object_count = 0
        bytes_used = 0
        objects, object_count, bytes_used = get_container_details(self.datadir)

        if int(self.metadata[X_OBJECTS_COUNT][0]) != object_count or \
           int(self.metadata[X_BYTES_USED][0]) != bytes_used:
            self.metadata[X_OBJECTS_COUNT] = (object_count, 0)
            self.metadata[X_BYTES_USED] = (bytes_used, 0)
            self.update_container(self.metadata)

    def update_container_count(self):
        containers = []
        container_count = 0

        containers, container_count = get_account_details(self.datadir)

        if int(self.metadata[X_CONTAINER_COUNT][0]) != container_count:
            self.metadata[X_CONTAINER_COUNT] = (container_count, 0)
            self.update_container(self.metadata)

    def get_info(self, include_metadata=False):
        """
        Get global data for the container.
        :returns: dict with keys: account, container, object_count, bytes_used,
                      hash, id, created_at, put_timestamp, delete_timestamp,
                      reported_put_timestamp, reported_delete_timestamp,
                      reported_object_count, and reported_bytes_used.
                  If include_metadata is set, metadata is included as a key
                  pointing to a dict of tuples of the metadata
        """
        # TODO: delete_timestamp, reported_put_timestamp
        #       reported_delete_timestamp, reported_object_count,
        #       reported_bytes_used, created_at

        metadata = {}
        if os.path.exists(self.datadir):
            metadata = _read_metadata(self.datadir)

        data = {'account' : self.account, 'container' : self.container,
                'object_count' : metadata.get(X_OBJECTS_COUNT, ('0', 0))[0],
                'bytes_used' : metadata.get(X_BYTES_USED, ('0',0))[0],
                'hash': '', 'id' : '', 'created_at' : '1',
                'put_timestamp' : metadata.get(X_PUT_TIMESTAMP, ('0',0))[0],
                'delete_timestamp' : '1',
                'reported_put_timestamp' : '1', 'reported_delete_timestamp' : '1',
                'reported_object_count' : '1', 'reported_bytes_used' : '1'}
        if include_metadata:
            data['metadata'] = metadata
        return data

    def put_object(self, name, timestamp, size, content_type,
                    etag, deleted=0):
        # TODO: Implement the specifics of this func.
        pass

    def initialize(self, timestamp):
        pass

    def update_put_timestamp(self, timestamp):
        """
        Create the container if it doesn't exist and update the timestamp
        """
        if not os.path.exists(self.datadir):
            self.put(self.metadata)

    def delete_object(self, name, timestamp):
        # TODO: Implement the delete object
        pass

    def delete_db(self, timestamp):
        """
        Delete the container
        """
        self.unlink()

    def update_metadata(self, metadata):
        assert self.metadata, "Valid container/account metadata should have been created by now"
        if metadata:
            new_metadata = self.metadata.copy()
            new_metadata.update(metadata)
            if new_metadata != self.metadata:
                write_metadata(self.datadir, new_metadata)
                self.metadata = new_metadata


class DiskAccount(DiskDir):
    def __init__(self, root, account, logger):
        super(DiskAccount, self).__init__(root, account, None, logger)
        assert self.dir_exists

    def list_containers_iter(self, limit, marker, end_marker,
                             prefix, delimiter):
        """
        Return tuple of name, object_count, bytes_used, 0(is_subdir).
        Used by account server.
        """
        if delimiter and not prefix:
            prefix = ''

        containers, container_count = get_account_details(self.datadir)

        if int(self.metadata[X_CONTAINER_COUNT][0]) != container_count:
            self.metadata[X_CONTAINER_COUNT] = (container_count, 0)
            self.update_container(self.metadata)

        if containers:
            containers.sort()

        if containers and prefix:
            containers = self.filter_prefix(containers, prefix)

        if containers and delimiter:
            containers = self.filter_delimiter(containers, delimiter, prefix)

        if containers and marker:
            containers = self.filter_marker(containers, marker)

        if containers and end_marker:
            containers = self.filter_end_marker(containers, end_marker)

        if containers and limit:
            if len(containers) > limit:
                containers = self.filter_limit(containers, limit)

        account_list = []
        if containers:
            for cont in containers:
                list_item = []
                metadata = None
                list_item.append(cont)
                cont_path = os.path.join(self.datadir, cont)
                metadata = _read_metadata(cont_path)
                if not metadata or not validate_container(metadata):
                    metadata = create_container_metadata(cont_path)

                if metadata:
                    list_item.append(metadata[X_OBJECTS_COUNT][0])
                    list_item.append(metadata[X_BYTES_USED][0])
                    list_item.append(0)
                account_list.append(list_item)

        return account_list

    def get_info(self, include_metadata=False):
        """
        Get global data for the account.
        :returns: dict with keys: account, created_at, put_timestamp,
                  delete_timestamp, container_count, object_count,
                  bytes_used, hash, id
        """
        metadata = {}
        if (os.path.exists(self.datadir)):
            metadata = _read_metadata(self.datadir)
            if not metadata:
                metadata = create_account_metadata(self.datadir)

        data = {'account' : self.account, 'created_at' : '1',
                'put_timestamp' : '1', 'delete_timestamp' : '1',
                'container_count' : metadata.get(X_CONTAINER_COUNT, (0,0))[0],
                'object_count' : metadata.get(X_OBJECTS_COUNT, (0,0))[0],
                'bytes_used' : metadata.get(X_BYTES_USED, (0,0))[0],
                'hash' : '', 'id' : ''}

        if include_metadata:
            data['metadata'] = metadata
        return data
