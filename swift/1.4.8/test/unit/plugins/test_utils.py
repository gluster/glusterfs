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

""" Tests for plugins.utils """

import os
import unittest
import errno
import xattr
import cPickle as pickle
import tempfile
import hashlib
from swift.common.utils import normalize_timestamp
from collections import defaultdict
from plugins import utils

#
# Somewhat hacky way of emulating the operation of xattr calls. They are made
# against a dictionary that stores the xattr key/value pairs.
#
_xattrs = {}
_xattr_op_cnt = defaultdict(int)
_xattr_err = {}

def _xkey(path, key):
    return "%s:%s" % (path, key)

def _setxattr(path, key, value):
    _xattr_op_cnt['set'] += 1
    xkey = _xkey(path, key)
    if xkey in _xattr_err:
        e = IOError()
        e.errno = _xattr_err[xkey]
        raise e
    global _xattrs
    _xattrs[xkey] = value

def _getxattr(path, key):
    _xattr_op_cnt['get'] += 1
    xkey = _xkey(path, key)
    if xkey in _xattr_err:
        e = IOError()
        e.errno = _xattr_err[xkey]
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
    if xkey in _xattr_err:
        e = IOError()
        e.errno = _xattr_err[xkey]
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
    global _xattr_err
    _xattr_err = {}

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


class TestUtils(unittest.TestCase):
    """ Tests for plugins.utils """

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
        _xattr_err[xkey] = errno.EOPNOTSUPP
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
        _xattr_err[xkey] = errno.EOPNOTSUPP
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
        _xattr_err[xkey] = errno.EOPNOTSUPP
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
        hd = utils.get_etag(tf.name)
        assert hd == hashlib.md5().hexdigest()

    def test_get_etag(self):
        tf = tempfile.NamedTemporaryFile()
        tf.file.write('123' * utils.CHUNK_SIZE)
        tf.file.flush()
        hd = utils.get_etag(tf.name)
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
        assert md[utils.X_ETAG] == utils.get_etag(tf.name)

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
        assert md[utils.X_ETAG] == utils.get_etag(tf.name)

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
        td = tempfile.mkdtemp()
        orig_gcd = utils.get_container_details
        utils.get_container_details = _mock_get_container_details
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
        td = tempfile.mkdtemp()
        orig_gad = utils.get_account_details
        utils.get_account_details = _mock_get_account_details
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
