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
import shutil
import random
import unittest
from tempfile import mkdtemp, mkstemp
from gluster.swift.common import fs_utils as fs
from gluster.swift.common.exceptions import NotDirectoryError, \
    FileOrDirNotFoundError

class TestUtils(unittest.TestCase):
    """ Tests for common.utils """

    def test_do_walk(self):
        try:
            # create directory structure
            tmpparent = mkdtemp()
            tmpdirs = []
            tmpfiles = []
            for i in range(5):
                tmpdirs.append(mkdtemp(dir=tmpparent).rsplit(os.path.sep, 1)[1])
                tmpfiles.append(mkstemp(dir=tmpparent)[1].rsplit(os.path.sep, \
                                                                     1)[1])

                for path, dirnames, filenames in fs.do_walk(tmpparent):
                    assert path == tmpparent
                    assert dirnames.sort() == tmpdirs.sort()
                    assert filenames.sort() == tmpfiles.sort()
                    break
        finally:
            shutil.rmtree(tmpparent)

    def test_do_open(self):
        try:
            fd, tmpfile = mkstemp()
            f = fs.do_open(tmpfile, 'r')
            try:
                f.write('test')
            except IOError as err:
                pass
            else:
                self.fail("IOError expected")
        finally:
            f.close()
            os.close(fd)
            os.remove(tmpfile)

    def test_do_open_err(self):
        try:
            fs.do_open(os.path.join('/tmp', str(random.random())), 'r')
        except IOError:
            pass
        else:
            self.fail("IOError expected")

    def test_do_write(self):
        try:
            fd, tmpfile = mkstemp()
            cnt = fs.do_write(fd, "test")
            assert cnt == len("test")
        finally:
            os.close(fd)
            os.remove(tmpfile)

    def test_do_write_err(self):
        try:
            fd, tmpfile = mkstemp()
            fd1 = os.open(tmpfile, os.O_RDONLY)
            fs.do_write(fd1, "test")
        except OSError:
            pass
        else:
            self.fail("OSError expected")
        finally:
            os.close(fd)
            os.close(fd1)

    def test_do_mkdir(self):
        try:
            path = os.path.join('/tmp', str(random.random()))
            fs.do_mkdir(path)
            assert os.path.exists(path)
            assert fs.do_mkdir(path)
        finally:
            os.rmdir(path)

    def test_do_mkdir_err(self):
        try:
            path = os.path.join('/tmp', str(random.random()), str(random.random()))
            fs.do_mkdir(path)
        except OSError:
            pass
        else:
            self.fail("OSError expected")


    def test_do_makedirs(self):
        try:
            subdir = os.path.join('/tmp', str(random.random()))
            path = os.path.join(subdir, str(random.random()))
            fs.do_makedirs(path)
            assert os.path.exists(path)
            assert fs.do_makedirs(path)
        finally:
            shutil.rmtree(subdir)

    def test_do_listdir(self):
        try:
            tmpdir = mkdtemp()
            subdir = []
            for i in range(5):
                subdir.append(mkdtemp(dir=tmpdir).rsplit(os.path.sep, 1)[1])

            assert subdir.sort() == fs.do_listdir(tmpdir).sort()
        finally:
            shutil.rmtree(tmpdir)

    def test_do_listdir_err(self):
        try:
            path = os.path.join('/tmp', str(random.random()))
            fs.do_listdir(path)
        except OSError:
            pass
        else:
            self.fail("OSError expected")

    def test_do_stat(self):
        try:
            tmpdir = mkdtemp()
            fd, tmpfile = mkstemp(dir=tmpdir)
            buf1 = os.stat(tmpfile)
            buf2 = fs.do_stat(fd)
            buf3 = fs.do_stat(tmpfile)

            assert buf1 == buf2
            assert buf1 == buf3
        finally:
            os.close(fd)
            os.remove(tmpfile)
            os.rmdir(tmpdir)

    def test_do_stat_err(self):
        try:
            fs.do_stat(os.path.join('/tmp', str(random.random())))
        except OSError:
            pass
        else:
            self.fail("OSError expected")

    def test_do_close(self):
        try:
            fd, tmpfile = mkstemp()
            fs.do_close(fd);
            try:
                os.write(fd, "test")
            except OSError:
                pass
            else:
                self.fail("OSError expected")
            fp = open(tmpfile)
            fs.do_close(fp)
        finally:
            os.remove(tmpfile)

    def test_do_unlink(self):
        try:
            fd, tmpfile = mkstemp()
            fs.do_unlink(tmpfile)
            assert not os.path.exists(tmpfile)
            assert fs.do_unlink(os.path.join('/tmp', str(random.random())))
        finally:
            os.close(fd)

    def test_do_unlink_err(self):
        try:
            tmpdir = mkdtemp()
            fs.do_unlink(tmpdir)
        except OSError:
            pass
        else:
            self.fail('OSError expected')
        finally:
            os.rmdir(tmpdir)

    def test_do_rmdir(self):
        tmpdir = mkdtemp()
        fs.do_rmdir(tmpdir)
        assert not os.path.exists(tmpdir)
        assert not fs.do_rmdir(os.path.join('/tmp', str(random.random())))

    def test_do_rmdir_err(self):
        try:
            fd, tmpfile = mkstemp()
            fs.do_rmdir(tmpfile)
        except OSError:
            pass
        else:
            self.fail('OSError expected')
        finally:
            os.close(fd)
            os.remove(tmpfile)

    def test_do_rename(self):
        try:
            srcpath = mkdtemp()
            destpath = os.path.join('/tmp', str(random.random()))
            fs.do_rename(srcpath, destpath)
            assert not os.path.exists(srcpath)
            assert os.path.exists(destpath)
        finally:
            os.rmdir(destpath)

    def test_do_rename_err(self):
        try:
            srcpath = os.path.join('/tmp', str(random.random()))
            destpath = os.path.join('/tmp', str(random.random()))
            fs.do_rename(srcpath, destpath)
        except OSError:
            pass
        else:
            self.fail("OSError expected")

    def test_dir_empty(self):
        try:
            tmpdir = mkdtemp()
            subdir = mkdtemp(dir=tmpdir)
            assert not fs.dir_empty(tmpdir)
            assert fs.dir_empty(subdir)
        finally:
            shutil.rmtree(tmpdir)

    def test_dir_empty_err(self):
        try:
            try:
                assert fs.dir_empty(os.path.join('/tmp', str(random.random())))
            except FileOrDirNotFoundError:
                pass
            else:
                self.fail("FileOrDirNotFoundError exception expected")

            fd, tmpfile = mkstemp()
            try:
                fs.dir_empty(tmpfile)
            except NotDirectoryError:
                pass
            else:
                self.fail("NotDirectoryError exception expected")
        finally:
            os.close(fd)
            os.unlink(tmpfile)

    def test_rmdirs(self):
        try:
            tmpdir = mkdtemp()
            subdir = mkdtemp(dir=tmpdir)
            fd, tmpfile = mkstemp(dir=tmpdir)
            assert not fs.rmdirs(tmpfile)
            assert not fs.rmdirs(tmpdir)
            assert fs.rmdirs(subdir)
            assert not os.path.exists(subdir)
        finally:
            os.close(fd)
            shutil.rmtree(tmpdir)
