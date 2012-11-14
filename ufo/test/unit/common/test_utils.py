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

""" Tests for common.utils """

import os
import unittest
import errno
import xattr
import cPickle as pickle
import tempfile
import hashlib
import tarfile
import shutil
from collections import defaultdict
from swift.common.utils import normalize_timestamp
from gluster.swift.common import utils

#
# Somewhat hacky way of emulating the operation of xattr calls. They are made
# against a dictionary that stores the xattr key/value pairs.
#
_xattrs = {}
_xattr_op_cnt = defaultdict(int)
_xattr_set_err = {}
_xattr_get_err = {}
_xattr_rem_err = {}

def _xkey(path, key):
    return "%s:%s" % (path, key)

def _setxattr(path, key, value):
    _xattr_op_cnt['set'] += 1
    xkey = _xkey(path, key)
    if xkey in _xattr_set_err:
        e = IOError()
        e.errno = _xattr_set_err[xkey]
        raise e
    global _xattrs
    _xattrs[xkey] = value

def _getxattr(path, key):
    _xattr_op_cnt['get'] += 1
    xkey = _xkey(path, key)
    if xkey in _xattr_get_err:
        e = IOError()
        e.errno = _xattr_get_err[xkey]
        raise e
    global _xattrs
    if xkey in _xattrs:
        ret_val = _xattrs[xkey]
    else:
        e = IOError("Fake IOError")
        e.errno = errno.ENODATA
        raise e
    return ret_val

def _removexattr(path, key):
    _xattr_op_cnt['remove'] += 1
    xkey = _xkey(path, key)
    if xkey in _xattr_rem_err:
        e = IOError()
        e.errno = _xattr_rem_err[xkey]
        raise e
    global _xattrs
    if xkey in _xattrs:
        del _xattrs[xkey]
    else:
        e = IOError("Fake IOError")
        e.errno = errno.ENODATA
        raise e

def _initxattr():
    global _xattrs
    _xattrs = {}
    global _xattr_op_cnt
    _xattr_op_cnt = defaultdict(int)
    global _xattr_set_err, _xattr_get_err, _xattr_rem_err
    _xattr_set_err = {}
    _xattr_get_err = {}
    _xattr_rem_err = {}

    # Save the current methods
    global _xattr_set;    _xattr_set    = xattr.set
    global _xattr_get;    _xattr_get    = xattr.get
    global _xattr_remove; _xattr_remove = xattr.remove

    # Monkey patch the calls we use with our internal unit test versions
    xattr.set    = _setxattr
    xattr.get    = _getxattr
    xattr.remove = _removexattr

def _destroyxattr():
    # Restore the current methods just in case
    global _xattr_set;    xattr.set    = _xattr_set
    global _xattr_get;    xattr.get    = _xattr_get
    global _xattr_remove; xattr.remove = _xattr_remove
    # Destroy the stored values and
    global _xattrs; _xattrs = None


class SimMemcache(object):
    def __init__(self):
        self._d = {}

    def get(self, key):
        return self._d.get(key, None)

    def set(self, key, value):
        self._d[key] = value


class TestUtils(unittest.TestCase):
    """ Tests for common.utils """

    def setUp(self):
        _initxattr()

    def tearDown(self):
        _destroyxattr()

    def test_write_metadata(self):
        path = "/tmp/foo/w"
        orig_d = { 'bar' : 'foo' }
        utils.write_metadata(path, orig_d)
        xkey = _xkey(path, utils.METADATA_KEY)
        assert len(_xattrs.keys()) == 1
        assert xkey in _xattrs
        assert orig_d == pickle.loads(_xattrs[xkey])
        assert _xattr_op_cnt['set'] == 1

    def test_write_metadata_err(self):
        path = "/tmp/foo/w"
        orig_d = { 'bar' : 'foo' }
        xkey = _xkey(path, utils.METADATA_KEY)
        _xattr_set_err[xkey] = errno.EOPNOTSUPP
        try:
            utils.write_metadata(path, orig_d)
        except IOError as e:
            assert e.errno == errno.EOPNOTSUPP
            assert len(_xattrs.keys()) == 0
            assert _xattr_op_cnt['set'] == 1
        else:
            self.fail("Expected an IOError exception on write")

    def test_write_metadata_multiple(self):
        # At 64 KB an xattr key/value pair, this should generate three keys.
        path = "/tmp/foo/w"
        orig_d = { 'bar' : 'x' * 150000 }
        utils.write_metadata(path, orig_d)
        assert len(_xattrs.keys()) == 3, "Expected 3 keys, found %d" % len(_xattrs.keys())
        payload = ''
        for i in range(0,3):
            xkey = _xkey(path, "%s%s" % (utils.METADATA_KEY, i or ''))
            assert xkey in _xattrs
            assert len(_xattrs[xkey]) <= utils.MAX_XATTR_SIZE
            payload += _xattrs[xkey]
        assert orig_d == pickle.loads(payload)
        assert _xattr_op_cnt['set'] == 3, "%r" % _xattr_op_cnt

    def test_clean_metadata(self):
        path = "/tmp/foo/c"
        expected_d = { 'a': 'y' * 150000 }
        expected_p = pickle.dumps(expected_d, utils.PICKLE_PROTOCOL)
        for i in range(0,3):
            xkey = _xkey(path, "%s%s" % (utils.METADATA_KEY, i or ''))
            _xattrs[xkey] = expected_p[:utils.MAX_XATTR_SIZE]
            expected_p = expected_p[utils.MAX_XATTR_SIZE:]
        assert not expected_p
        utils.clean_metadata(path)
        assert _xattr_op_cnt['remove'] == 4, "%r" % _xattr_op_cnt

    def test_clean_metadata_err(self):
        path = "/tmp/foo/c"
        xkey = _xkey(path, utils.METADATA_KEY)
        _xattrs[xkey] = pickle.dumps({ 'a': 'y' }, utils.PICKLE_PROTOCOL)
        _xattr_rem_err[xkey] = errno.EOPNOTSUPP
        try:
            utils.clean_metadata(path)
        except IOError as e:
            assert e.errno == errno.EOPNOTSUPP
            assert _xattr_op_cnt['remove'] == 1, "%r" % _xattr_op_cnt
        else:
            self.fail("Expected an IOError exception on remove")

    def test_read_metadata(self):
        path = "/tmp/foo/r"
        expected_d = { 'a': 'y' }
        xkey = _xkey(path, utils.METADATA_KEY)
        _xattrs[xkey] = pickle.dumps(expected_d, utils.PICKLE_PROTOCOL)
        res_d = utils.read_metadata(path)
        assert res_d == expected_d, "Expected %r, result %r" % (expected_d, res_d)
        assert _xattr_op_cnt['get'] == 1, "%r" % _xattr_op_cnt

    def test_read_metadata_notfound(self):
        path = "/tmp/foo/r"
        res_d = utils.read_metadata(path)
        assert res_d == {}
        assert _xattr_op_cnt['get'] == 1, "%r" % _xattr_op_cnt

    def test_read_metadata_err(self):
        path = "/tmp/foo/r"
        expected_d = { 'a': 'y' }
        xkey = _xkey(path, utils.METADATA_KEY)
        _xattrs[xkey] = pickle.dumps(expected_d, utils.PICKLE_PROTOCOL)
        _xattr_get_err[xkey] = errno.EOPNOTSUPP
        try:
            res_d = utils.read_metadata(path)
        except IOError as e:
            assert e.errno == errno.EOPNOTSUPP
            assert (_xattr_op_cnt['get'] == 1), "%r" % _xattr_op_cnt
        else:
            self.fail("Expected an IOError exception on get")

    def test_read_metadata_multiple(self):
        path = "/tmp/foo/r"
        expected_d = { 'a': 'y' * 150000 }
        expected_p = pickle.dumps(expected_d, utils.PICKLE_PROTOCOL)
        for i in range(0,3):
            xkey = _xkey(path, "%s%s" % (utils.METADATA_KEY, i or ''))
            _xattrs[xkey] = expected_p[:utils.MAX_XATTR_SIZE]
            expected_p = expected_p[utils.MAX_XATTR_SIZE:]
        assert not expected_p
        res_d = utils.read_metadata(path)
        assert res_d == expected_d, "Expected %r, result %r" % (expected_d, res_d)
        assert _xattr_op_cnt['get'] == 3, "%r" % _xattr_op_cnt

    def test_read_metadata_multiple_one_missing(self):
        path = "/tmp/foo/r"
        expected_d = { 'a': 'y' * 150000 }
        expected_p = pickle.dumps(expected_d, utils.PICKLE_PROTOCOL)
        for i in range(0,2):
            xkey = _xkey(path, "%s%s" % (utils.METADATA_KEY, i or ''))
            _xattrs[xkey] = expected_p[:utils.MAX_XATTR_SIZE]
            expected_p = expected_p[utils.MAX_XATTR_SIZE:]
        assert len(expected_p) <= utils.MAX_XATTR_SIZE
        res_d = utils.read_metadata(path)
        assert res_d == {}
        assert _xattr_op_cnt['get'] == 3, "%r" % _xattr_op_cnt
        assert len(_xattrs.keys()) == 0, "Expected 0 keys, found %d" % len(_xattrs.keys())

    def test_restore_metadata_none(self):
        # No initial metadata
        path = "/tmp/foo/i"
        res_d = utils.restore_metadata(path, { 'b': 'y' })
        expected_d = { 'b': 'y' }
        assert res_d == expected_d, "Expected %r, result %r" % (expected_d, res_d)
        assert _xattr_op_cnt['get'] == 1, "%r" % _xattr_op_cnt
        assert _xattr_op_cnt['set'] == 1, "%r" % _xattr_op_cnt

    def test_restore_metadata(self):
        # Initial metadata
        path = "/tmp/foo/i"
        initial_d = { 'a': 'z' }
        xkey = _xkey(path, utils.METADATA_KEY)
        _xattrs[xkey] = pickle.dumps(initial_d, utils.PICKLE_PROTOCOL)
        res_d = utils.restore_metadata(path, { 'b': 'y' })
        expected_d = { 'a': 'z', 'b': 'y' }
        assert res_d == expected_d, "Expected %r, result %r" % (expected_d, res_d)
        assert _xattr_op_cnt['get'] == 1, "%r" % _xattr_op_cnt
        assert _xattr_op_cnt['set'] == 1, "%r" % _xattr_op_cnt

    def test_restore_metadata_nochange(self):
        # Initial metadata but no changes
        path = "/tmp/foo/i"
        initial_d = { 'a': 'z' }
        xkey = _xkey(path, utils.METADATA_KEY)
        _xattrs[xkey] = pickle.dumps(initial_d, utils.PICKLE_PROTOCOL)
        res_d = utils.restore_metadata(path, {})
        expected_d = { 'a': 'z' }
        assert res_d == expected_d, "Expected %r, result %r" % (expected_d, res_d)
        assert _xattr_op_cnt['get'] == 1, "%r" % _xattr_op_cnt
        assert _xattr_op_cnt['set'] == 0, "%r" % _xattr_op_cnt

    def test_add_timestamp_empty(self):
        orig = {}
        res = utils._add_timestamp(orig)
        assert res == {}

    def test_add_timestamp_none(self):
        orig = { 'a': 1, 'b': 2, 'c': 3 }
        exp = { 'a': (1, 0), 'b': (2, 0), 'c': (3, 0) }
        res = utils._add_timestamp(orig)
        assert res == exp

    def test_add_timestamp_mixed(self):
        orig = { 'a': 1, 'b': (2, 1), 'c': 3 }
        exp = { 'a': (1, 0), 'b': (2, 1), 'c': (3, 0) }
        res = utils._add_timestamp(orig)
        assert res == exp

    def test_add_timestamp_all(self):
        orig = { 'a': (1, 0), 'b': (2, 1), 'c': (3, 0) }
        res = utils._add_timestamp(orig)
        assert res == orig

    def test_get_etag_empty(self):
        tf = tempfile.NamedTemporaryFile()
        hd = utils._get_etag(tf.name)
        assert hd == hashlib.md5().hexdigest()

    def test_get_etag(self):
        tf = tempfile.NamedTemporaryFile()
        tf.file.write('123' * utils.CHUNK_SIZE)
        tf.file.flush()
        hd = utils._get_etag(tf.name)
        tf.file.seek(0)
        md5 = hashlib.md5()
        while True:
            chunk = tf.file.read(utils.CHUNK_SIZE)
            if not chunk:
                break
            md5.update(chunk)
        assert hd == md5.hexdigest()

    def test_get_object_metadata_dne(self):
        md = utils.get_object_metadata("/tmp/doesNotEx1st")
        assert md == {}

    def test_get_object_metadata_err(self):
        tf = tempfile.NamedTemporaryFile()
        try:
            md = utils.get_object_metadata(os.path.join(tf.name,"doesNotEx1st"))
        except OSError as e:
            assert e.errno != errno.ENOENT
        else:
            self.fail("Expected exception")

    obj_keys = (utils.X_TIMESTAMP, utils.X_CONTENT_TYPE, utils.X_ETAG,
                utils.X_CONTENT_LENGTH, utils.X_TYPE, utils.X_OBJECT_TYPE)

    def test_get_object_metadata_file(self):
        tf = tempfile.NamedTemporaryFile()
        tf.file.write('123'); tf.file.flush()
        md = utils.get_object_metadata(tf.name)
        for key in self.obj_keys:
            assert key in md, "Expected key %s in %r" % (key, md)
        assert md[utils.X_TYPE] == utils.OBJECT
        assert md[utils.X_OBJECT_TYPE] == utils.FILE
        assert md[utils.X_CONTENT_TYPE] == utils.FILE_TYPE
        assert md[utils.X_CONTENT_LENGTH] == os.path.getsize(tf.name)
        assert md[utils.X_TIMESTAMP] == normalize_timestamp(os.path.getctime(tf.name))
        assert md[utils.X_ETAG] == utils._get_etag(tf.name)

    def test_get_object_metadata_dir(self):
        td = tempfile.mkdtemp()
        try:
            md = utils.get_object_metadata(td)
            for key in self.obj_keys:
                assert key in md, "Expected key %s in %r" % (key, md)
            assert md[utils.X_TYPE] == utils.OBJECT
            assert md[utils.X_OBJECT_TYPE] == utils.DIR
            assert md[utils.X_CONTENT_TYPE] == utils.DIR_TYPE
            assert md[utils.X_CONTENT_LENGTH] == 0
            assert md[utils.X_TIMESTAMP] == normalize_timestamp(os.path.getctime(td))
            assert md[utils.X_ETAG] == hashlib.md5().hexdigest()
        finally:
            os.rmdir(td)

    def test_create_object_metadata_file(self):
        tf = tempfile.NamedTemporaryFile()
        tf.file.write('4567'); tf.file.flush()
        r_md = utils.create_object_metadata(tf.name)

        xkey = _xkey(tf.name, utils.METADATA_KEY)
        assert len(_xattrs.keys()) == 1
        assert xkey in _xattrs
        assert _xattr_op_cnt['get'] == 1
        assert _xattr_op_cnt['set'] == 1
        md = pickle.loads(_xattrs[xkey])
        assert r_md == md

        for key in self.obj_keys:
            assert key in md, "Expected key %s in %r" % (key, md)
        assert md[utils.X_TYPE] == utils.OBJECT
        assert md[utils.X_OBJECT_TYPE] == utils.FILE
        assert md[utils.X_CONTENT_TYPE] == utils.FILE_TYPE
        assert md[utils.X_CONTENT_LENGTH] == os.path.getsize(tf.name)
        assert md[utils.X_TIMESTAMP] == normalize_timestamp(os.path.getctime(tf.name))
        assert md[utils.X_ETAG] == utils._get_etag(tf.name)

    def test_create_object_metadata_dir(self):
        td = tempfile.mkdtemp()
        try:
            r_md = utils.create_object_metadata(td)

            xkey = _xkey(td, utils.METADATA_KEY)
            assert len(_xattrs.keys()) == 1
            assert xkey in _xattrs
            assert _xattr_op_cnt['get'] == 1
            assert _xattr_op_cnt['set'] == 1
            md = pickle.loads(_xattrs[xkey])
            assert r_md == md

            for key in self.obj_keys:
                assert key in md, "Expected key %s in %r" % (key, md)
            assert md[utils.X_TYPE] == utils.OBJECT
            assert md[utils.X_OBJECT_TYPE] == utils.DIR
            assert md[utils.X_CONTENT_TYPE] == utils.DIR_TYPE
            assert md[utils.X_CONTENT_LENGTH] == 0
            assert md[utils.X_TIMESTAMP] == normalize_timestamp(os.path.getctime(td))
            assert md[utils.X_ETAG] == hashlib.md5().hexdigest()
        finally:
            os.rmdir(td)

    def test_get_container_metadata(self):
        def _mock_get_container_details(path, memcache=None):
            o_list = [ 'a', 'b', 'c' ]
            o_count = 3
            b_used = 47
            return o_list, o_count, b_used
        orig_gcd = utils.get_container_details
        utils.get_container_details = _mock_get_container_details
        td = tempfile.mkdtemp()
        try:
            exp_md = {
                utils.X_TYPE: (utils.CONTAINER, 0),
                utils.X_TIMESTAMP: (normalize_timestamp(os.path.getctime(td)), 0),
                utils.X_PUT_TIMESTAMP: (normalize_timestamp(os.path.getmtime(td)), 0),
                utils.X_OBJECTS_COUNT: (3, 0),
                utils.X_BYTES_USED: (47, 0),
                }
            md = utils.get_container_metadata(td)
            assert md == exp_md
        finally:
            utils.get_container_details = orig_gcd
            os.rmdir(td)

    def test_get_account_metadata(self):
        def _mock_get_account_details(path, memcache=None):
            c_list = [ '123', 'abc' ]
            c_count = 2
            return c_list, c_count
        orig_gad = utils.get_account_details
        utils.get_account_details = _mock_get_account_details
        td = tempfile.mkdtemp()
        try:
            exp_md = {
                utils.X_TYPE: (utils.ACCOUNT, 0),
                utils.X_TIMESTAMP: (normalize_timestamp(os.path.getctime(td)), 0),
                utils.X_PUT_TIMESTAMP: (normalize_timestamp(os.path.getmtime(td)), 0),
                utils.X_OBJECTS_COUNT: (0, 0),
                utils.X_BYTES_USED: (0, 0),
                utils.X_CONTAINER_COUNT: (2, 0),
                }
            md = utils.get_account_metadata(td)
            assert md == exp_md
        finally:
            utils.get_account_details = orig_gad
            os.rmdir(td)

    cont_keys = [utils.X_TYPE, utils.X_TIMESTAMP, utils.X_PUT_TIMESTAMP,
                 utils.X_OBJECTS_COUNT, utils.X_BYTES_USED]

    def test_create_container_metadata(self):
        td = tempfile.mkdtemp()
        try:
            r_md = utils.create_container_metadata(td)

            xkey = _xkey(td, utils.METADATA_KEY)
            assert len(_xattrs.keys()) == 1
            assert xkey in _xattrs
            assert _xattr_op_cnt['get'] == 1
            assert _xattr_op_cnt['set'] == 1
            md = pickle.loads(_xattrs[xkey])
            assert r_md == md

            for key in self.cont_keys:
                assert key in md, "Expected key %s in %r" % (key, md)
            assert md[utils.X_TYPE] == (utils.CONTAINER, 0)
            assert md[utils.X_TIMESTAMP] == (normalize_timestamp(os.path.getctime(td)), 0)
            assert md[utils.X_PUT_TIMESTAMP] == (normalize_timestamp(os.path.getmtime(td)), 0)
            assert md[utils.X_OBJECTS_COUNT] == (0, 0)
            assert md[utils.X_BYTES_USED] == (0, 0)
        finally:
            os.rmdir(td)

    acct_keys = [val for val in cont_keys]
    acct_keys.append(utils.X_CONTAINER_COUNT)

    def test_create_account_metadata(self):
        td = tempfile.mkdtemp()
        try:
            r_md = utils.create_account_metadata(td)

            xkey = _xkey(td, utils.METADATA_KEY)
            assert len(_xattrs.keys()) == 1
            assert xkey in _xattrs
            assert _xattr_op_cnt['get'] == 1
            assert _xattr_op_cnt['set'] == 1
            md = pickle.loads(_xattrs[xkey])
            assert r_md == md

            for key in self.acct_keys:
                assert key in md, "Expected key %s in %r" % (key, md)
            assert md[utils.X_TYPE] == (utils.ACCOUNT, 0)
            assert md[utils.X_TIMESTAMP] == (normalize_timestamp(os.path.getctime(td)), 0)
            assert md[utils.X_PUT_TIMESTAMP] == (normalize_timestamp(os.path.getmtime(td)), 0)
            assert md[utils.X_OBJECTS_COUNT] == (0, 0)
            assert md[utils.X_BYTES_USED] == (0, 0)
            assert md[utils.X_CONTAINER_COUNT] == (0, 0)
        finally:
            os.rmdir(td)

    def test_container_details_uncached(self):
        the_path = "/tmp/bar"
        def mock_get_container_details_from_fs(cont_path):
            bu = 5
            oc = 1
            ol = ['foo',]
            dl = [('a',100),]
            return utils.ContainerDetails(bu, oc, ol, dl)
        orig_gcdff = utils._get_container_details_from_fs
        utils._get_container_details_from_fs = mock_get_container_details_from_fs
        try:
            retval = utils.get_container_details(the_path)
            cd = mock_get_container_details_from_fs(the_path)
            assert retval == (cd.obj_list, cd.object_count, cd.bytes_used)
        finally:
            utils._get_container_details_from_fs = orig_gcdff

    def test_container_details_cached_hit(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_container_details_from_fs(cont_path, bu_p=5):
            bu = bu_p
            oc = 1
            ol = ['foo',]
            dl = [('a',100),]
            return utils.ContainerDetails(bu, oc, ol, dl)
        def mock_do_stat(path):
            class MockStat(object):
                def __init__(self, mtime):
                    self.st_mtime = mtime
            return MockStat(100)
        cd = mock_get_container_details_from_fs(the_path, bu_p=6)
        mc.set(utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path, cd)
        orig_gcdff = utils._get_container_details_from_fs
        utils._get_container_details_from_fs = mock_get_container_details_from_fs
        orig_ds = utils.do_stat
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_container_details(the_path, memcache=mc)
            # If it did not properly use memcache, the default mocked version
            # of get details from fs would return 5 bytes used instead of the
            # 6 we specified above.
            cd = mock_get_container_details_from_fs(the_path, bu_p=6)
            assert retval == (cd.obj_list, cd.object_count, cd.bytes_used)
        finally:
            utils._get_container_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_container_details_cached_miss_key(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_container_details_from_fs(cont_path, bu_p=5):
            bu = bu_p
            oc = 1
            ol = ['foo',]
            dl = [('a',100),]
            return utils.ContainerDetails(bu, oc, ol, dl)
        def mock_do_stat(path):
            # Be sure we don't miss due to mtimes not matching
            self.fail("do_stat should not have been called")
        cd = mock_get_container_details_from_fs(the_path + "u", bu_p=6)
        mc.set(utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path + "u", cd)
        orig_gcdff = utils._get_container_details_from_fs
        utils._get_container_details_from_fs = mock_get_container_details_from_fs
        orig_ds = utils.do_stat
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_container_details(the_path, memcache=mc)
            cd = mock_get_container_details_from_fs(the_path)
            assert retval == (cd.obj_list, cd.object_count, cd.bytes_used)
            mkey = utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path
            assert mkey in mc._d
        finally:
            utils._get_container_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_container_details_cached_miss_dir_list(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_container_details_from_fs(cont_path, bu_p=5):
            bu = bu_p
            oc = 1
            ol = ['foo',]
            dl = []
            return utils.ContainerDetails(bu, oc, ol, dl)
        def mock_do_stat(path):
            # Be sure we don't miss due to mtimes not matching
            self.fail("do_stat should not have been called")
        cd = mock_get_container_details_from_fs(the_path, bu_p=6)
        mc.set(utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path, cd)
        orig_gcdff = utils._get_container_details_from_fs
        utils._get_container_details_from_fs = mock_get_container_details_from_fs
        orig_ds = utils.do_stat
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_container_details(the_path, memcache=mc)
            cd = mock_get_container_details_from_fs(the_path)
            assert retval == (cd.obj_list, cd.object_count, cd.bytes_used)
            mkey = utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path
            assert mkey in mc._d
            assert 5 == mc._d[mkey].bytes_used
        finally:
            utils._get_container_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_container_details_cached_miss_mtime(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_container_details_from_fs(cont_path, bu_p=5):
            bu = bu_p
            oc = 1
            ol = ['foo',]
            dl = [('a',100),]
            return utils.ContainerDetails(bu, oc, ol, dl)
        def mock_do_stat(path):
            # Be sure we miss due to mtimes not matching
            class MockStat(object):
                def __init__(self, mtime):
                    self.st_mtime = mtime
            return MockStat(200)
        cd = mock_get_container_details_from_fs(the_path, bu_p=6)
        mc.set(utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path, cd)
        orig_gcdff = utils._get_container_details_from_fs
        utils._get_container_details_from_fs = mock_get_container_details_from_fs
        orig_ds = utils.do_stat
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_container_details(the_path, memcache=mc)
            cd = mock_get_container_details_from_fs(the_path)
            assert retval == (cd.obj_list, cd.object_count, cd.bytes_used)
            mkey = utils.MEMCACHE_CONTAINER_DETAILS_KEY_PREFIX + the_path
            assert mkey in mc._d
            assert 5 == mc._d[mkey].bytes_used
        finally:
            utils._get_container_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_account_details_uncached(self):
        the_path = "/tmp/bar"
        def mock_get_account_details_from_fs(acc_path, acc_stats):
            mt = 100
            cc = 2
            cl = ['a', 'b']
            return utils.AccountDetails(mt, cc, cl)
        orig_gcdff = utils._get_account_details_from_fs
        utils._get_account_details_from_fs = mock_get_account_details_from_fs
        try:
            retval = utils.get_account_details(the_path)
            ad = mock_get_account_details_from_fs(the_path, None)
            assert retval == (ad.container_list, ad.container_count)
        finally:
            utils._get_account_details_from_fs = orig_gcdff

    def test_account_details_cached_hit(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_account_details_from_fs(acc_path, acc_stats):
            mt = 100
            cc = 2
            cl = ['a', 'b']
            return utils.AccountDetails(mt, cc, cl)
        def mock_do_stat(path):
            class MockStat(object):
                def __init__(self, mtime):
                    self.st_mtime = mtime
            return MockStat(100)
        ad = mock_get_account_details_from_fs(the_path, None)
        ad.container_list = ['x', 'y']
        mc.set(utils.MEMCACHE_ACCOUNT_DETAILS_KEY_PREFIX + the_path, ad)
        orig_gcdff = utils._get_account_details_from_fs
        orig_ds = utils.do_stat
        utils._get_account_details_from_fs = mock_get_account_details_from_fs
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_account_details(the_path, memcache=mc)
            assert retval == (ad.container_list, ad.container_count)
            wrong_ad = mock_get_account_details_from_fs(the_path, None)
            assert wrong_ad != ad
        finally:
            utils._get_account_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_account_details_cached_miss(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_account_details_from_fs(acc_path, acc_stats):
            mt = 100
            cc = 2
            cl = ['a', 'b']
            return utils.AccountDetails(mt, cc, cl)
        def mock_do_stat(path):
            class MockStat(object):
                def __init__(self, mtime):
                    self.st_mtime = mtime
            return MockStat(100)
        ad = mock_get_account_details_from_fs(the_path, None)
        ad.container_list = ['x', 'y']
        mc.set(utils.MEMCACHE_ACCOUNT_DETAILS_KEY_PREFIX + the_path + 'u', ad)
        orig_gcdff = utils._get_account_details_from_fs
        orig_ds = utils.do_stat
        utils._get_account_details_from_fs = mock_get_account_details_from_fs
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_account_details(the_path, memcache=mc)
            correct_ad = mock_get_account_details_from_fs(the_path, None)
            assert retval == (correct_ad.container_list, correct_ad.container_count)
            assert correct_ad != ad
        finally:
            utils._get_account_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_account_details_cached_miss_mtime(self):
        mc = SimMemcache()
        the_path = "/tmp/bar"
        def mock_get_account_details_from_fs(acc_path, acc_stats):
            mt = 100
            cc = 2
            cl = ['a', 'b']
            return utils.AccountDetails(mt, cc, cl)
        def mock_do_stat(path):
            class MockStat(object):
                def __init__(self, mtime):
                    self.st_mtime = mtime
            return MockStat(100)
        ad = mock_get_account_details_from_fs(the_path, None)
        ad.container_list = ['x', 'y']
        ad.mtime = 200
        mc.set(utils.MEMCACHE_ACCOUNT_DETAILS_KEY_PREFIX + the_path, ad)
        orig_gcdff = utils._get_account_details_from_fs
        orig_ds = utils.do_stat
        utils._get_account_details_from_fs = mock_get_account_details_from_fs
        utils.do_stat = mock_do_stat
        try:
            retval = utils.get_account_details(the_path, memcache=mc)
            correct_ad = mock_get_account_details_from_fs(the_path, None)
            assert retval == (correct_ad.container_list, correct_ad.container_count)
            assert correct_ad != ad
        finally:
            utils._get_account_details_from_fs = orig_gcdff
            utils.do_stat = orig_ds

    def test_get_container_details_from_fs(self):
        orig_cwd = os.getcwd()
        td = tempfile.mkdtemp()
        try:
            tf = tarfile.open("common/data/account_tree.tar.bz2", "r:bz2")
            os.chdir(td)
            tf.extractall()

            ad = utils._get_account_details_from_fs(td, None)
            assert ad.mtime == os.path.getmtime(td)
            assert ad.container_count == 3
            assert set(ad.container_list) == set(['c1', 'c2', 'c3'])
        finally:
            os.chdir(orig_cwd)
            shutil.rmtree(td)

    def test_get_container_details_from_fs_notadir(self):
        tf = tempfile.NamedTemporaryFile()
        cd = utils._get_container_details_from_fs(tf.name)
        assert cd.bytes_used == 0
        assert cd.object_count == 0
        assert cd.obj_list == []
        assert cd.dir_list == []

    def test_get_account_details_from_fs(self):
        orig_cwd = os.getcwd()
        td = tempfile.mkdtemp()
        try:
            tf = tarfile.open("common/data/container_tree.tar.bz2", "r:bz2")
            os.chdir(td)
            tf.extractall()

            cd = utils._get_container_details_from_fs(td)
            assert cd.bytes_used == 30, repr(cd.bytes_used)
            assert cd.object_count == 8, repr(cd.object_count)
            assert set(cd.obj_list) == set(['file1', 'file3', 'file2',
                                   'dir3', 'dir1', 'dir2',
                                   'dir1/file1', 'dir1/file2'
                                   ]), repr(cd.obj_list)
            full_dir1 = os.path.join(td, 'dir1')
            full_dir2 = os.path.join(td, 'dir2')
            full_dir3 = os.path.join(td, 'dir3')
            exp_dir_dict = { td:        os.path.getmtime(td),
                             full_dir1: os.path.getmtime(full_dir1),
                             full_dir2: os.path.getmtime(full_dir2),
                             full_dir3: os.path.getmtime(full_dir3),
                             }
            for d,m in cd.dir_list:
                assert d in exp_dir_dict
                assert exp_dir_dict[d] == m
        finally:
            os.chdir(orig_cwd)
            shutil.rmtree(td)

    def test_get_account_details_from_fs_notadir_w_stats(self):
        tf = tempfile.NamedTemporaryFile()
        ad = utils._get_account_details_from_fs(tf.name, os.stat(tf.name))
        assert ad.mtime == os.path.getmtime(tf.name)
        assert ad.container_count == 0
        assert ad.container_list == []

    def test_get_account_details_from_fs_notadir(self):
        tf = tempfile.NamedTemporaryFile()
        ad = utils._get_account_details_from_fs(tf.name, None)
        assert ad.mtime == os.path.getmtime(tf.name)
        assert ad.container_count == 0
        assert ad.container_list == []

    def test_write_pickle(self):
        td = tempfile.mkdtemp()
        try:
            fpp = os.path.join(td, 'pp')
            utils.write_pickle('pickled peppers', fpp)
            with open(fpp, "rb") as f:
                contents = f.read()
            s = pickle.loads(contents)
            assert s == 'pickled peppers', repr(s)
        finally:
            shutil.rmtree(td)

    def test_write_pickle_ignore_tmp(self):
        tf = tempfile.NamedTemporaryFile()
        td = tempfile.mkdtemp()
        try:
            fpp = os.path.join(td, 'pp')
            # Also test an explicity pickle protocol
            utils.write_pickle('pickled peppers', fpp, tmp=tf.name, pickle_protocol=2)
            with open(fpp, "rb") as f:
                contents = f.read()
            s = pickle.loads(contents)
            assert s == 'pickled peppers', repr(s)
            with open(tf.name, "rb") as f:
                contents = f.read()
            assert contents == ''
        finally:
            shutil.rmtree(td)

    def test_check_user_xattr_bad_path(self):
        assert False == utils.check_user_xattr("/tmp/foo/bar/check/user/xattr")

    def test_check_user_xattr_bad_set(self):
        td = tempfile.mkdtemp()
        xkey = _xkey(td, 'user.test.key1')
        _xattr_set_err[xkey] = errno.EOPNOTSUPP
        try:
            assert False == utils.check_user_xattr(td)
        except IOError:
            pass
        else:
            self.fail("Expected IOError")
        finally:
            shutil.rmtree(td)

    def test_check_user_xattr_bad_remove(self):
        td = tempfile.mkdtemp()
        xkey = _xkey(td, 'user.test.key1')
        _xattr_rem_err[xkey] = errno.EOPNOTSUPP
        try:
            utils.check_user_xattr(td)
        except IOError:
            self.fail("Unexpected IOError")
        finally:
            shutil.rmtree(td)

    def test_check_user_xattr(self):
        td = tempfile.mkdtemp()
        try:
            assert utils.check_user_xattr(td)
        finally:
            shutil.rmtree(td)

    def test_validate_container_empty(self):
        ret = utils.validate_container({})
        assert ret == False

    def test_validate_container_missing_keys(self):
        ret = utils.validate_container({ 'foo': 'bar' })
        assert ret == False

    def test_validate_container_bad_type(self):
        md = { utils.X_TYPE: ('bad', 0),
               utils.X_TIMESTAMP: ('na', 0),
               utils.X_PUT_TIMESTAMP: ('na', 0),
               utils.X_OBJECTS_COUNT: ('na', 0),
               utils.X_BYTES_USED: ('na', 0) }
        ret = utils.validate_container(md)
        assert ret == False

    def test_validate_container_good_type(self):
        md = { utils.X_TYPE: (utils.CONTAINER, 0),
               utils.X_TIMESTAMP: ('na', 0),
               utils.X_PUT_TIMESTAMP: ('na', 0),
               utils.X_OBJECTS_COUNT: ('na', 0),
               utils.X_BYTES_USED: ('na', 0) }
        ret = utils.validate_container(md)
        assert ret

    def test_validate_account_empty(self):
        ret = utils.validate_account({})
        assert ret == False

    def test_validate_account_missing_keys(self):
        ret = utils.validate_account({ 'foo': 'bar' })
        assert ret == False

    def test_validate_account_bad_type(self):
        md = { utils.X_TYPE: ('bad', 0),
               utils.X_TIMESTAMP: ('na', 0),
               utils.X_PUT_TIMESTAMP: ('na', 0),
               utils.X_OBJECTS_COUNT: ('na', 0),
               utils.X_BYTES_USED: ('na', 0),
               utils.X_CONTAINER_COUNT: ('na', 0) }
        ret = utils.validate_account(md)
        assert ret == False

    def test_validate_account_good_type(self):
        md = { utils.X_TYPE: (utils.ACCOUNT, 0),
               utils.X_TIMESTAMP: ('na', 0),
               utils.X_PUT_TIMESTAMP: ('na', 0),
               utils.X_OBJECTS_COUNT: ('na', 0),
               utils.X_BYTES_USED: ('na', 0),
               utils.X_CONTAINER_COUNT: ('na', 0) }
        ret = utils.validate_account(md)
        assert ret

    def test_validate_object_empty(self):
        ret = utils.validate_object({})
        assert ret == False

    def test_validate_object_missing_keys(self):
        ret = utils.validate_object({ 'foo': 'bar' })
        assert ret == False

    def test_validate_object_bad_type(self):
        md = { utils.X_TIMESTAMP: 'na',
               utils.X_CONTENT_TYPE: 'na',
               utils.X_ETAG: 'bad',
               utils.X_CONTENT_LENGTH: 'na',
               utils.X_TYPE: 'bad',
               utils.X_OBJECT_TYPE: 'na' }
        ret = utils.validate_object(md)
        assert ret == False

    def test_validate_object_good_type(self):
        md = { utils.X_TIMESTAMP: 'na',
               utils.X_CONTENT_TYPE: 'na',
               utils.X_ETAG: 'bad',
               utils.X_CONTENT_LENGTH: 'na',
               utils.X_TYPE: utils.OBJECT,
               utils.X_OBJECT_TYPE: 'na' }
        ret = utils.validate_object(md)
        assert ret

    def test_is_marker_empty(self):
        assert False == utils.is_marker(None)

    def test_is_marker_missing(self):
        assert False == utils.is_marker( { 'foo': 'bar' } )

    def test_is_marker_not_marker(self):
        md = { utils.X_OBJECT_TYPE: utils.DIR }
        assert False == utils.is_marker(md)

    def test_is_marker(self):
        md = { utils.X_OBJECT_TYPE: utils.MARKER_DIR }
        assert utils.is_marker(md)
