# Copyright (c) 2013 Red Hat, Inc.
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

import unittest
import os, fcntl, errno, shutil
from tempfile import mkdtemp
import gluster.swift.common.Glusterfs as gfs

def mock_os_path_ismount(path):
    return True

def mock_get_export_list():
    return ['test', 'test2']

def mock_os_system(cmd):
    return False

def mock_fcntl_lockf(f, *a, **kw):
    raise IOError(errno.EAGAIN)

def _init():
    global _RUN_DIR, _OS_SYSTEM, _FCNTL_LOCKF
    global _OS_PATH_ISMOUNT, __GET_EXPORT_LIST

    _RUN_DIR          = gfs.RUN_DIR
    _OS_SYSTEM        = os.system
    _FCNTL_LOCKF      = fcntl.lockf
    _OS_PATH_ISMOUNT  = os.path.ismount
    __GET_EXPORT_LIST = gfs._get_export_list

def _init_mock_variables(tmpdir):
    os.system            = mock_os_system
    os.path.ismount      = mock_os_path_ismount
    gfs.RUN_DIR          = os.path.join(tmpdir, 'var/run/swift')
    gfs._get_export_list = mock_get_export_list

def _reset_mock_variables():
    gfs.RUN_DIR          = _RUN_DIR
    gfs._get_export_list = __GET_EXPORT_LIST

    os.system       = _OS_SYSTEM
    fcntl.lockf     = _FCNTL_LOCKF
    os.path.ismount = _OS_PATH_ISMOUNT

class TestGlusterfs(unittest.TestCase):
    """ Tests for common.GlusterFS """

    def setUp(self):
        _init()

    def test_mount(self):
        try:
            tmpdir = mkdtemp()
            root   = os.path.join(tmpdir, 'mnt/gluster-object')
            drive  = 'test'

            _init_mock_variables(tmpdir)
            assert gfs.mount(root, drive)
        finally:
            _reset_mock_variables()
            shutil.rmtree(tmpdir)

    def test_mount_egain(self):
        try:
            tmpdir = mkdtemp()
            root   = os.path.join(tmpdir, 'mnt/gluster-object')
            drive  = 'test'

            _init_mock_variables(tmpdir)
            assert gfs.mount(root, drive)
            fcntl.lockf  = mock_fcntl_lockf
            assert gfs.mount(root, drive)
        finally:
            _reset_mock_variables()
            shutil.rmtree(tmpdir)

    def test_mount_get_export_list_err(self):
        gfs._get_export_list = mock_get_export_list
        assert not gfs.mount(None, 'drive')
        _reset_mock_variables()

    def tearDown(self):
        _reset_mock_variables()
