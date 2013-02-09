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

""" Tests for gluster.swift.common.DiskFile """

import os
import stat
import errno
import unittest
import tempfile
import shutil
from hashlib import md5
from swift.common.utils import normalize_timestamp
from swift.common.exceptions import DiskFileNotExist
import gluster.swift.common.DiskFile
import gluster.swift.common.utils
from gluster.swift.common.DiskFile import Gluster_DiskFile, \
    AlreadyExistsAsDir
from gluster.swift.common.utils import DEFAULT_UID, DEFAULT_GID, X_TYPE, \
    X_OBJECT_TYPE
from test_utils import _initxattr, _destroyxattr
from test.unit import FakeLogger


_metadata = {}

def _mock_read_metadata(filename):
    if filename in _metadata:
        md = _metadata[filename]
    else:
        md = {}
    return md

def _mock_write_metadata(filename, metadata):
    _metadata[filename] = metadata

def _mock_clear_metadata():
    _metadata = {}


class MockException(Exception):
    pass


def _mock_rmdirs(p):
    raise MockException("gluster.swift.common.DiskFile.rmdirs() called")

def _mock_do_listdir(p):
    raise MockException("gluster.swift.common.DiskFile.do_listdir() called")

def _mock_do_unlink(f):
    ose = OSError()
    ose.errno = errno.ENOENT
    raise ose


def _mock_do_unlink_eacces_err(f):
    ose = OSError()
    ose.errno = errno.EACCES
    raise ose

def _mock_getsize_eaccess_err(f):
    ose = OSError()
    ose.errno = errno.EACCES
    raise ose

def _mock_do_rmdir_eacces_err(f):
    ose = OSError()
    ose.errno = errno.EACCES
    raise ose

class MockRenamerCalled(Exception):
    pass


def _mock_renamer(a, b):
    raise MockRenamerCalled()


class TestDiskFile(unittest.TestCase):
    """ Tests for gluster.swift.common.DiskFile """

    def setUp(self):
        self.lg = FakeLogger()
        _initxattr()
        _mock_clear_metadata()
        self._saved_df_wm = gluster.swift.common.DiskFile.write_metadata
        self._saved_df_rm = gluster.swift.common.DiskFile.read_metadata
        gluster.swift.common.DiskFile.write_metadata = _mock_write_metadata
        gluster.swift.common.DiskFile.read_metadata = _mock_read_metadata
        self._saved_ut_wm = gluster.swift.common.utils.write_metadata
        self._saved_ut_rm = gluster.swift.common.utils.read_metadata
        gluster.swift.common.utils.write_metadata = _mock_write_metadata
        gluster.swift.common.utils.read_metadata = _mock_read_metadata

    def tearDown(self):
        self.lg = None
        _destroyxattr()
        gluster.swift.common.DiskFile.write_metadata = self._saved_df_wm
        gluster.swift.common.DiskFile.read_metadata = self._saved_df_rm
        gluster.swift.common.utils.write_metadata = self._saved_ut_wm
        gluster.swift.common.utils.read_metadata = self._saved_ut_rm

    def test_constructor_no_slash(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        assert gdf._obj == "z"
        assert gdf._obj_path == ""
        assert gdf.name == "bar"
        assert gdf.datadir == "/tmp/foo/vol0/bar"
        assert gdf.device_path == "/tmp/foo/vol0"
        assert gdf._container_path == "/tmp/foo/vol0/bar"
        assert gdf.disk_chunk_size == 65536
        assert gdf.iter_hook == None
        assert gdf.logger == self.lg
        assert gdf.uid == DEFAULT_UID
        assert gdf.gid == DEFAULT_GID
        assert gdf.metadata == {}
        assert gdf.meta_file == None
        assert gdf.data_file == None
        assert gdf.fp == None
        assert gdf.iter_etag == None
        assert not gdf.started_at_0
        assert not gdf.read_to_eof
        assert gdf.quarantined_dir == None
        assert not gdf.keep_cache
        assert not gdf._is_dir

    def test_constructor_leadtrail_slash(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "/b/a/z/", self.lg)
        assert gdf._obj == "z"
        assert gdf._obj_path == "b/a"
        assert gdf.name == "bar/b/a"
        assert gdf.datadir == "/tmp/foo/vol0/bar/b/a"
        assert gdf.device_path == "/tmp/foo/vol0"

    def test_constructor_no_metadata(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            stats = os.stat(the_file)
            ts = normalize_timestamp(stats.st_ctime)
            etag = md5()
            etag.update("1234")
            etag = etag.hexdigest()
            exp_md = {
                'Content-Length': 4,
                'ETag': etag,
                'X-Timestamp': ts,
                'Content-Type': 'application/octet-stream'}
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            assert gdf.fp is None
            assert gdf.metadata == exp_md
        finally:
            shutil.rmtree(td)

    def test_constructor_existing_metadata(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            ini_md = {
                'X-Type': 'Object',
                'X-Object-Type': 'file',
                'Content-Length': 5,
                'ETag': 'etag',
                'X-Timestamp': 'ts',
                'Content-Type': 'application/loctet-stream'}
            _metadata[the_file] = ini_md
            exp_md = ini_md.copy()
            del exp_md['X-Type']
            del exp_md['X-Object-Type']
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            assert gdf.fp is None
            assert gdf.metadata == exp_md
        finally:
            shutil.rmtree(td)

    def test_constructor_invalid_existing_metadata(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        inv_md = {
            'Content-Length': 5,
            'ETag': 'etag',
            'X-Timestamp': 'ts',
            'Content-Type': 'application/loctet-stream'}
        _metadata[the_file] = inv_md
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            assert gdf.fp is None
            assert gdf.metadata != inv_md
        finally:
            shutil.rmtree(td)

    def test_constructor_isdir(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "d")
        try:
            os.makedirs(the_dir)
            ini_md = {
                'X-Type': 'Object',
                'X-Object-Type': 'dir',
                'Content-Length': 5,
                'ETag': 'etag',
                'X-Timestamp': 'ts',
                'Content-Type': 'application/loctet-stream'}
            _metadata[the_dir] = ini_md
            exp_md = ini_md.copy()
            del exp_md['X-Type']
            del exp_md['X-Object-Type']
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "d", self.lg, keep_data_fp=True)
            assert gdf._obj == "d"
            assert gdf.data_file == the_dir
            assert gdf._is_dir
            assert gdf.fp is None
            assert gdf.metadata == exp_md
        finally:
            shutil.rmtree(td)

    def test_constructor_keep_data_fp(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg, keep_data_fp=True)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            assert gdf.fp is not None
        finally:
            shutil.rmtree(td)

    def test_constructor_chunk_size(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg, disk_chunk_size=8192)
        assert gdf.disk_chunk_size == 8192

    def test_constructor_iter_hook(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg, iter_hook='hook')
        assert gdf.iter_hook == 'hook'

    def test_close(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        # Should be a no-op, as by default is_dir is False, but fp is None
        gdf.close()

        gdf._is_dir = True
        gdf.fp = "123"
        # Should still be a no-op as is_dir is True (marker directory)
        gdf.close()
        assert gdf.fp == "123"

        gdf._is_dir = False
        saved_dc = gluster.swift.common.DiskFile.do_close
        self.called = False
        def our_do_close(fp):
            self.called = True
        gluster.swift.common.DiskFile.do_close = our_do_close
        try:
            gdf.close()
            assert self.called
            assert gdf.fp is None
        finally:
            gluster.swift.common.DiskFile.do_close = saved_dc

    def test_is_deleted(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        assert gdf.is_deleted()
        gdf.data_file = "/tmp/foo/bar"
        assert not gdf.is_deleted()

    def test_create_dir_object(self):
        td = tempfile.mkdtemp()
        the_dir = os.path.join(td, "vol0", "bar", "dir")
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir/z", self.lg)
            # Not created, dir object path is different, just checking
            assert gdf._obj == "z"
            gdf._create_dir_object(the_dir)
            assert os.path.isdir(the_dir)
            assert the_dir in _metadata
        finally:
            shutil.rmtree(td)

    def test_create_dir_object_exists(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            os.makedirs(the_path)
            with open(the_dir, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir/z", self.lg)
            # Not created, dir object path is different, just checking
            assert gdf._obj == "z"
            def _mock_do_chown(p, u, g):
                assert u == DEFAULT_UID
                assert g == DEFAULT_GID
            dc = gluster.swift.common.DiskFile.do_chown
            gluster.swift.common.DiskFile.do_chown = _mock_do_chown
            try:
                gdf._create_dir_object(the_dir)
            finally:
                gluster.swift.common.DiskFile.do_chown = dc
            assert os.path.isdir(the_dir)
            assert the_dir in _metadata
        finally:
            shutil.rmtree(td)

    def test_put_metadata(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "z")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            md = { 'Content-Type': 'application/octet-stream', 'a': 'b' }
            gdf.put_metadata(md.copy())
            assert gdf.metadata == md, "gdf.metadata = %r, md = %r" % (gdf.metadata, md)
            assert _metadata[the_dir] == md
        finally:
            shutil.rmtree(td)

    def test_put_w_tombstone(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        assert gdf.metadata == {}

        gdf.put_metadata({'x': '1'}, tombstone=True)
        assert gdf.metadata == {}

    def test_put_w_meta_file(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            newmd = gdf.metadata.copy()
            newmd['X-Object-Meta-test'] = '1234'
            gdf.put_metadata(newmd)
            assert gdf.metadata == newmd
            assert _metadata[the_file] == newmd
        finally:
            shutil.rmtree(td)

    def test_put_w_meta_file_no_content_type(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            newmd = gdf.metadata.copy()
            newmd['Content-Type'] = ''
            newmd['X-Object-Meta-test'] = '1234'
            gdf.put_metadata(newmd)
            assert gdf.metadata == newmd
            assert _metadata[the_file] == newmd
        finally:
            shutil.rmtree(td)

    def test_put_w_meta_dir(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir", self.lg)
            newmd = gdf.metadata.copy()
            newmd['X-Object-Meta-test'] = '1234'
            gdf.put_metadata(newmd)
            assert gdf.metadata == newmd
            assert _metadata[the_dir] == newmd
        finally:
            shutil.rmtree(td)

    def test_put_w_marker_dir(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir", self.lg)
            newmd = gdf.metadata.copy()
            newmd['X-Object-Meta-test'] = '1234'
            gdf.put_metadata(newmd)
            assert gdf.metadata == newmd
            assert _metadata[the_dir] == newmd
        finally:
            shutil.rmtree(td)

    def test_put_w_marker_dir_create(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir", self.lg)
            assert gdf.metadata == {}
            newmd = {
                'Content-Length': 0,
                'ETag': 'etag',
                'X-Timestamp': 'ts',
                'Content-Type': 'application/directory'}
            gdf.put(None, newmd, extension='.dir')
            assert gdf.data_file == the_dir
            assert gdf.metadata == newmd
            assert _metadata[the_dir] == newmd
        finally:
            shutil.rmtree(td)

    def test_put_is_dir(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir", self.lg)
            origmd = gdf.metadata.copy()
            origfmd = _metadata[the_dir]
            newmd = gdf.metadata.copy()
            # FIXME: This is a hack to get to the code-path; it is not clear
            # how this can happen normally.
            newmd['Content-Type'] = ''
            newmd['X-Object-Meta-test'] = '1234'
            try:
                gdf.put(None, newmd, extension='.data')
            except AlreadyExistsAsDir:
                pass
            else:
                self.fail("Expected to encounter 'already-exists-as-dir' exception")
            assert gdf.metadata == origmd
            assert _metadata[the_dir] == origfmd
        finally:
            shutil.rmtree(td)

    def test_put(self):
        td = tempfile.mkdtemp()
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf._obj_path == ""
            assert gdf.name == "bar"
            assert gdf.datadir == os.path.join(td, "vol0", "bar")
            assert gdf.data_file is None

            body = '1234\n'
            etag = md5()
            etag.update(body)
            etag = etag.hexdigest()
            metadata = {
                'X-Timestamp': '1234',
                'Content-Type': 'file',
                'ETag': etag,
                'Content-Length': '5',
                }

            with gdf.mkstemp() as fd:
                assert gdf.tmppath is not None
                tmppath = gdf.tmppath
                os.write(fd, body)
                gdf.put(fd, metadata)

            assert gdf.data_file == os.path.join(td, "vol0", "bar", "z")
            assert os.path.exists(gdf.data_file)
            assert not os.path.exists(tmppath)
        finally:
            shutil.rmtree(td)

    def test_put_obj_path(self):
        the_obj_path = os.path.join("b", "a")
        the_file = os.path.join(the_obj_path, "z")
        td = tempfile.mkdtemp()
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   the_file, self.lg)
            assert gdf._obj == "z"
            assert gdf._obj_path == the_obj_path
            assert gdf.name == os.path.join("bar", "b", "a")
            assert gdf.datadir == os.path.join(td, "vol0", "bar", "b", "a")
            assert gdf.data_file is None

            body = '1234\n'
            etag = md5()
            etag.update(body)
            etag = etag.hexdigest()
            metadata = {
                'X-Timestamp': '1234',
                'Content-Type': 'file',
                'ETag': etag,
                'Content-Length': '5',
                }

            with gdf.mkstemp() as fd:
                assert gdf.tmppath is not None
                tmppath = gdf.tmppath
                os.write(fd, body)
                gdf.put(fd, metadata)

            assert gdf.data_file == os.path.join(td, "vol0", "bar", "b", "a", "z")
            assert os.path.exists(gdf.data_file)
            assert not os.path.exists(tmppath)
        finally:
            shutil.rmtree(td)

    def test_unlinkold_no_metadata(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        assert gdf.metadata == {}
        _saved_rmdirs = gluster.swift.common.DiskFile.rmdirs
        _saved_do_listdir = gluster.swift.common.DiskFile.do_listdir
        gluster.swift.common.DiskFile.rmdirs = _mock_rmdirs
        gluster.swift.common.DiskFile.do_listdir = _mock_do_listdir
        try:
            gdf.unlinkold(None)
        except MockException as exp:
            self.fail(str(exp))
        finally:
            gluster.swift.common.DiskFile.rmdirs = _saved_rmdirs
            gluster.swift.common.DiskFile.do_listdir = _saved_do_listdir

    def test_unlinkold_same_timestamp(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        assert gdf.metadata == {}
        gdf.metadata['X-Timestamp'] = 1
        _saved_rmdirs = gluster.swift.common.DiskFile.rmdirs
        _saved_do_listdir = gluster.swift.common.DiskFile.do_listdir
        gluster.swift.common.DiskFile.rmdirs = _mock_rmdirs
        gluster.swift.common.DiskFile.do_listdir = _mock_do_listdir
        try:
            gdf.unlinkold(1)
        except MockException as exp:
            self.fail(str(exp))
        finally:
            gluster.swift.common.DiskFile.rmdirs = _saved_rmdirs
            gluster.swift.common.DiskFile.do_listdir = _saved_do_listdir

    def test_unlinkold_file(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir

            later = float(gdf.metadata['X-Timestamp']) + 1
            gdf.unlinkold(normalize_timestamp(later))
            assert os.path.isdir(gdf.datadir)
            assert not os.path.exists(os.path.join(gdf.datadir, gdf._obj))
        finally:
            shutil.rmtree(td)

    def test_unlinkold_file_not_found(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir

            # Handle the case the file is not in the directory listing.
            os.unlink(the_file)

            later = float(gdf.metadata['X-Timestamp']) + 1
            gdf.unlinkold(normalize_timestamp(later))
            assert os.path.isdir(gdf.datadir)
            assert not os.path.exists(os.path.join(gdf.datadir, gdf._obj))
        finally:
            shutil.rmtree(td)

    def test_unlinkold_file_unlink_error(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir

            later = float(gdf.metadata['X-Timestamp']) + 1

            stats = os.stat(the_path)
            os.chmod(the_path, stats.st_mode & (~stat.S_IWUSR))

            # Handle the case do_unlink() raises an OSError
            __os_unlink = os.unlink
            os.unlink = _mock_do_unlink_eacces_err
            try:
                gdf.unlinkold(normalize_timestamp(later))
            except OSError as e:
                assert e.errno == errno.EACCES
            else:
                self.fail("Excepted an OSError when unlinking file")
            finally:
                os.unlink = __os_unlink
                os.chmod(the_path, stats.st_mode)

            assert os.path.isdir(gdf.datadir)
            assert os.path.exists(os.path.join(gdf.datadir, gdf._obj))
        finally:
            shutil.rmtree(td)

    def test_unlinkold_is_dir(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "d")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "d", self.lg, keep_data_fp=True)
            assert gdf.data_file == the_dir
            assert gdf._is_dir

            later = float(gdf.metadata['X-Timestamp']) + 1
            gdf.unlinkold(normalize_timestamp(later))
            assert os.path.isdir(gdf.datadir)
            assert not os.path.exists(os.path.join(gdf.datadir, gdf._obj))
        finally:
            shutil.rmtree(td)

    def test_unlinkold_is_dir_failure(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "d")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "d", self.lg, keep_data_fp=True)
            assert gdf.data_file == the_dir
            assert gdf._is_dir

            stats = os.stat(gdf.datadir)
            os.chmod(gdf.datadir, 0)
            __os_rmdir = os.rmdir
            os.rmdir = _mock_do_rmdir_eacces_err
            try:
                later = float(gdf.metadata['X-Timestamp']) + 1
                gdf.unlinkold(normalize_timestamp(later))
            finally:
                os.chmod(gdf.datadir, stats.st_mode)
                os.rmdir = __os_rmdir
            assert os.path.isdir(gdf.datadir)
            assert os.path.isdir(gdf.data_file)
        finally:
            shutil.rmtree(td)

    def test_get_data_file_size(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            assert 4 == gdf.get_data_file_size()
        finally:
            shutil.rmtree(td)

    def test_get_data_file_size(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            assert 4 == gdf.metadata['Content-Length']
            gdf.metadata['Content-Length'] = 3
            assert 4 == gdf.get_data_file_size()
            assert 4 == gdf.metadata['Content-Length']
        finally:
            shutil.rmtree(td)

    def test_get_data_file_size_dne(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "/b/a/z/", self.lg)
        try:
            s = gdf.get_data_file_size()
        except DiskFileNotExist:
            pass
        else:
            self.fail("Expected DiskFileNotExist exception")

    def test_get_data_file_size_dne_os_err(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            gdf.data_file = gdf.data_file + ".dne"
            try:
                s = gdf.get_data_file_size()
            except DiskFileNotExist:
                pass
            else:
                self.fail("Expected DiskFileNotExist exception")
        finally:
            shutil.rmtree(td)

    def test_get_data_file_size_os_err(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_file = os.path.join(the_path, "z")
        try:
            os.makedirs(the_path)
            with open(the_file, "wb") as fd:
                fd.write("1234")
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "z", self.lg)
            assert gdf._obj == "z"
            assert gdf.data_file == the_file
            assert not gdf._is_dir
            stats = os.stat(the_path)
            os.chmod(the_path, 0)
            __os_path_getsize = os.path.getsize
            os.path.getsize = _mock_getsize_eaccess_err
            try:
                s = gdf.get_data_file_size()
            except OSError as err:
                assert err.errno == errno.EACCES
            else:
                self.fail("Expected OSError exception")
            finally:
                os.path.getsize = __os_path_getsize
                os.chmod(the_path, stats.st_mode)
        finally:
            shutil.rmtree(td)

    def test_get_data_file_size_dir(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "d")
        try:
            os.makedirs(the_dir)
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "d", self.lg, keep_data_fp=True)
            assert gdf._obj == "d"
            assert gdf.data_file == the_dir
            assert gdf._is_dir
            assert 0 == gdf.get_data_file_size()
        finally:
            shutil.rmtree(td)

    def test_filter_metadata(self):
        assert not os.path.exists("/tmp/foo")
        gdf = Gluster_DiskFile("/tmp/foo", "vol0", "p57", "ufo47", "bar",
                               "z", self.lg)
        assert gdf.metadata == {}
        gdf.filter_metadata()
        assert gdf.metadata == {}

        gdf.metadata[X_TYPE] = 'a'
        gdf.metadata[X_OBJECT_TYPE] = 'b'
        gdf.metadata['foobar'] = 'c'
        gdf.filter_metadata()
        assert X_TYPE not in gdf.metadata
        assert X_OBJECT_TYPE not in gdf.metadata
        assert 'foobar' in gdf.metadata

    def test_mkstemp(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir/z", self.lg)
            saved_tmppath = ''
            with gdf.mkstemp() as fd:
                assert gdf.datadir == os.path.join(td, "vol0", "bar", "dir")
                assert os.path.isdir(gdf.datadir)
                saved_tmppath = gdf.tmppath
                assert os.path.dirname(saved_tmppath) == gdf.datadir
                assert os.path.basename(saved_tmppath)[:3] == '.z.'
                assert os.path.exists(saved_tmppath)
                os.write(fd, "123")
            assert not os.path.exists(saved_tmppath)
        finally:
            shutil.rmtree(td)

    def test_mkstemp_err_on_close(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir/z", self.lg)
            saved_tmppath = ''
            with gdf.mkstemp() as fd:
                assert gdf.datadir == os.path.join(td, "vol0", "bar", "dir")
                assert os.path.isdir(gdf.datadir)
                saved_tmppath = gdf.tmppath
                assert os.path.dirname(saved_tmppath) == gdf.datadir
                assert os.path.basename(saved_tmppath)[:3] == '.z.'
                assert os.path.exists(saved_tmppath)
                os.write(fd, "123")
            # At the end of previous with block a close on fd is called.
            # Calling os.close on the same fd will raise an OSError
            # exception and we must catch it.
            try:
                os.close(fd)
            except OSError as err:
                pass
            else:
                self.fail("Exception expected")
            assert not os.path.exists(saved_tmppath)
        finally:
            shutil.rmtree(td)

    def test_mkstemp_err_on_unlink(self):
        td = tempfile.mkdtemp()
        the_path = os.path.join(td, "vol0", "bar")
        the_dir = os.path.join(the_path, "dir")
        try:
            gdf = Gluster_DiskFile(td, "vol0", "p57", "ufo47", "bar",
                                   "dir/z", self.lg)
            saved_tmppath = ''
            with gdf.mkstemp() as fd:
                assert gdf.datadir == os.path.join(td, "vol0", "bar", "dir")
                assert os.path.isdir(gdf.datadir)
                saved_tmppath = gdf.tmppath
                assert os.path.dirname(saved_tmppath) == gdf.datadir
                assert os.path.basename(saved_tmppath)[:3] == '.z.'
                assert os.path.exists(saved_tmppath)
                os.write(fd, "123")
                os.unlink(saved_tmppath)
            assert not os.path.exists(saved_tmppath)
        finally:
            shutil.rmtree(td)
