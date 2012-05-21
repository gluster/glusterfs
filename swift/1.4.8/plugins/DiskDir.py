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

import os

from swift.plugins.utils import clean_metadata, dir_empty, rmdirs, mkdirs, \
     validate_account, validate_container, check_valid_account, is_marker, \
     get_container_details, get_account_details, create_container_metadata, \
     create_account_metadata, DEFAULT_GID, DEFAULT_UID, get_account_details, \
     validate_object, create_object_metadata, read_metadata, write_metadata

from swift.common.constraints import CONTAINER_LISTING_LIMIT, \
    check_mount

from swift.plugins.utils import X_CONTENT_TYPE, X_CONTENT_LENGTH, X_TIMESTAMP,\
     X_PUT_TIMESTAMP, X_TYPE, X_ETAG, X_OBJECTS_COUNT, X_BYTES_USED, \
     X_CONTAINER_COUNT, CONTAINER

from swift import plugins
def strip_obj_storage_path(path, string='/mnt/gluster-object'):
    """
    strip /mnt/gluster-object
    """
    return path.replace(string, '').strip('/')

DATADIR = 'containers'


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

    def update_account(self, metadata):
        acc_path = self.datadir
        write_metadata(acc_path, metadata)
        self.metadata = metadata

class DiskDir(DiskCommon):
    """
    Manage object files on disk.

    :param path: path to devices on the node
    :param device: device name
    :param partition: partition on the device the object lives in
    :param account: account name for the object
    :param container: container name for the object
    :param obj: object name for the object
    :param keep_data_fp: if True, don't close the fp, otherwise close it
    :param disk_chunk_Size: size of chunks on file reads
    """

    def __init__(self, path, device, partition, account, container, logger,
                 uid=DEFAULT_UID, gid=DEFAULT_GID, fs_object=None):
        self.root = path
        device = account
        if container:
            self.name = container
        else:
            self.name = None
        if self.name:
            self.datadir = os.path.join(path, account, self.name)
        else:
            self.datadir = os.path.join(path, device)
        self.account = account
        self.device_path = os.path.join(path, device)
        if not check_mount(path, device):
            check_valid_account(account, fs_object)
        self.logger = logger
        self.metadata = {}
        self.uid = int(uid)
        self.gid = int(gid)
        # Create a dummy db_file in /etc/swift
        self.db_file = '/etc/swift/db_file.db'
        if not os.path.exists(self.db_file):
            file(self.db_file, 'w+')
        self.dir_exists = os.path.exists(self.datadir)
        if self.dir_exists:
            try:
                self.metadata = read_metadata(self.datadir)
            except EOFError:
                create_container_metadata(self.datadir)
        else:
            return
        if container:
            if not self.metadata:
                create_container_metadata(self.datadir)
                self.metadata = read_metadata(self.datadir)
            else:
                if not validate_container(self.metadata):
                    create_container_metadata(self.datadir)
                    self.metadata = read_metadata(self.datadir)
        else:
            if not self.metadata:
                create_account_metadata(self.datadir)
                self.metadata = read_metadata(self.datadir)
            else:
                if not validate_account(self.metadata):
                    create_account_metadata(self.datadir)
                    self.metadata = read_metadata(self.datadir)

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
        self.metadata[X_OBJECTS_COUNT] = int(self.metadata[X_OBJECTS_COUNT]) + 1
        self.metadata[X_PUT_TIMESTAMP] = timestamp
        self.metadata[X_BYTES_USED] = int(self.metadata[X_BYTES_USED]) + int(content_length)
        #TODO: define update_metadata instad of writing whole metadata again.
        self.put_metadata(self.metadata)

    def delete_obj(self, content_length):
        self.metadata[X_OBJECTS_COUNT] = int(self.metadata[X_OBJECTS_COUNT]) - 1
        self.metadata[X_BYTES_USED] = int(self.metadata[X_BYTES_USED]) - int(content_length)
        self.put_metadata(self.metadata)

    def put_container(self, container, put_timestamp, del_timestamp, object_count, bytes_used):
        """
        For account server.
        """
        self.metadata[X_OBJECTS_COUNT] = 0
        self.metadata[X_BYTES_USED] = 0
        self.metadata[X_CONTAINER_COUNT] = int(self.metadata[X_CONTAINER_COUNT]) + 1
        self.metadata[X_PUT_TIMESTAMP] = 1
        self.put_metadata(self.metadata)

    def delete_container(self, object_count, bytes_used):
        """
        For account server.
        """
        self.metadata[X_OBJECTS_COUNT] = 0
        self.metadata[X_BYTES_USED] = 0
        self.metadata[X_CONTAINER_COUNT] = int(self.metadata[X_CONTAINER_COUNT]) - 1
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

        objects = []
        object_count = 0
        bytes_used = 0
        container_list = []

        objects, object_count, bytes_used = get_container_details(self.datadir)

        if int(self.metadata[X_OBJECTS_COUNT]) != object_count or \
           int(self.metadata[X_BYTES_USED]) != bytes_used:
            self.metadata[X_OBJECTS_COUNT] = object_count
            self.metadata[X_BYTES_USED] = bytes_used
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

        if objects:
            for obj in objects:
                list_item = []
                list_item.append(obj)
                metadata = read_metadata(self.datadir + '/' + obj)
                if not metadata or not validate_object(metadata):
                    metadata = create_object_metadata(self.datadir + '/' + obj)
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


        if int(self.metadata[X_OBJECTS_COUNT]) != object_count or \
           int(self.metadata[X_BYTES_USED]) != bytes_used:
            self.metadata[X_OBJECTS_COUNT] = object_count
            self.metadata[X_BYTES_USED] = bytes_used
            self.update_container(self.metadata)

    def update_container_count(self):
        containers = []
        container_count = 0

        containers, container_count = get_account_details(self.datadir)

        if int(self.metadata[X_CONTAINER_COUNT]) != container_count:
            self.metadata[X_CONTAINER_COUNT] = container_count
            self.update_account(self.metadata)

    def get_info(self, include_metadata=False):
        """
        Get global data for the container.
        :returns: dict with keys: account, container, created_at,
                  put_timestamp, delete_timestamp, object_count, bytes_used,
                  reported_put_timestamp, reported_delete_timestamp,
                  reported_object_count, reported_bytes_used, hash, id,
                  x_container_sync_point1, and x_container_sync_point2.
                  If include_metadata is set, metadata is included as a key
                  pointing to a dict of tuples of the metadata
        """
        # TODO: delete_timestamp, reported_put_timestamp
        #       reported_delete_timestamp, reported_object_count,
        #       reported_bytes_used, created_at

        metadata = {}
        if os.path.exists(self.datadir):
            metadata = read_metadata(self.datadir)

        data = {'account' : self.account, 'container' : self.name,
                'object_count' : metadata.get(X_OBJECTS_COUNT, '0'),
                'bytes_used' : metadata.get(X_BYTES_USED, '0'),
                'hash': '', 'id' : '', 'created_at' : '1',
                'put_timestamp' : metadata.get(X_PUT_TIMESTAMP, '0'),
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
        self.metadata.update(metadata)
        write_metadata(self.datadir, self.metadata)


class DiskAccount(DiskDir):
    def __init__(self, root, account, fs_object = None):
        self.root = root
        self.account = account
        self.datadir = os.path.join(self.root, self.account)
        if not check_mount(root, account):
            check_valid_account(account, fs_object)
        self.metadata = read_metadata(self.datadir)
        if not self.metadata or not validate_account(self.metadata):
            self.metadata = create_account_metadata(self.datadir)

    def list_containers_iter(self, limit, marker, end_marker,
                             prefix, delimiter):
        """
        Return tuple of name, object_count, bytes_used, 0(is_subdir).
        Used by account server.
        """
        if delimiter and not prefix:
            prefix = ''
        containers = []
        container_count = 0
        account_list = []

        containers, container_count = get_account_details(self.datadir)

        if int(self.metadata[X_CONTAINER_COUNT]) != container_count:
            self.metadata[X_CONTAINER_COUNT] = container_count
            self.update_account(self.metadata)

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

        if containers:
            for cont in containers:
                list_item = []
                metadata = None
                list_item.append(cont)
                metadata = read_metadata(self.datadir + '/' + cont)
                if not metadata or not validate_container(metadata):
                    metadata = create_container_metadata(self.datadir + '/' + cont)

                if metadata:
                    list_item.append(metadata[X_OBJECTS_COUNT])
                    list_item.append(metadata[X_BYTES_USED])
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
            metadata = read_metadata(self.datadir)
            if not metadata:
                metadata = create_account_metadata(self.datadir)

        data = {'account' : self.account, 'created_at' : '1',
                'put_timestamp' : '1', 'delete_timestamp' : '1',
                'container_count' : metadata.get(X_CONTAINER_COUNT, 0),
                'object_count' : metadata.get(X_OBJECTS_COUNT, 0),
                'bytes_used' : metadata.get(X_BYTES_USED, 0),
                'hash' : '', 'id' : ''}

        if include_metadata:
            data['metadata'] = metadata
        return data
