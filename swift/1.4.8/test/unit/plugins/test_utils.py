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

import unittest
import errno
import xattr
import cPickle as pickle
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
