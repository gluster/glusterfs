/*
   Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/*
 * LD PRELOAD Test Tool
 *
 * 1. The idea of this test tool is to check the sanity of the LD_PRELOAD
 * mechanism in libc for the system on which booster needs to run.
 *
 * 2. Basically, this test tool calls various system calls for which there is
 * support in the booster library. Combined with the logging output from this
 * tool and the preload-test-lib, one can determine whether the
 * pre-loading mechanism is working working in order for booster to initialize.
 *
 * 3. This tool does not test GlusterFS functionality running under booster
 * although the path specified to the tool can be a GlusterFS mountpoint but
 * that is not very useful.
 *
 * 4. This tool is incomplete without the preload-test-lib and the
 * accompanyung shell script that needs to be run for running the test.
 */
#define _XOPEN_SOURCE 500

#include <dlfcn.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <utime.h>
#include <sys/time.h>
#include <attr/xattr.h>
#include <sys/sendfile.h>


#define PRELOAD_ERRNO_VERF      6449
void
check_err(int ret, char *call, int tabs)
{
        while (tabs > 0) {
                fprintf (stdout, "\t");
                --tabs;
        }
        if (ret != -1) {
                fprintf (stdout, "Not intercepted: %s\n", call);
                return;
        }

        if (errno != PRELOAD_ERRNO_VERF) {
                fprintf (stdout, "Not intercepted: %s: err: %s\n", call,
                         strerror (errno));
                return;
        }

        fprintf (stdout, "Intercept verified: %s\n", call);
        return;
}

void
usage (FILE *fp)
{
        fprintf (fp, "Usage: ld-preload-test <Options>\n");
        fprintf (fp, "Options\n");
        fprintf (fp, "\t--path\t\tPathname is used as the file/directory"
                     " created for the test.\n");

}


int
run_file_tests (char *testfile)
{
        int             ret = -1;
        struct stat     buf;

        assert (testfile);
        fprintf (stdout, "Testing creat");
        ret = creat (testfile, S_IRWXU);
        check_err (ret, "creat", 2);

        fprintf (stdout, "Testing close");
        ret = close (ret);
        check_err (ret, "close", 2);

        fprintf (stdout, "Testing open");
        ret = open (testfile, O_RDONLY);
        check_err (ret, "open", 2);

        fprintf (stdout, "Testing read");
        ret = read (0, NULL, 0);
        check_err (ret, "read", 2);

        fprintf (stdout, "Testing readv");
        ret = readv (0, NULL, 0);
        check_err (ret, "readv", 2);

        fprintf (stdout, "Testing pread");
        ret = pread (0, NULL, 0, 0);
        check_err (ret, "pread", 2);

        fprintf (stdout, "Testing write");
        ret = write (0, NULL, 0);
        check_err (ret, "write", 2);

        fprintf (stdout, "Testing writev");
        ret = writev (0, NULL, 0);
        check_err (ret, "writev", 2);

        fprintf (stdout, "Testing pwrite");
        ret = pwrite (0, NULL, 0, 0);
        check_err (ret, "pwrite", 2);

        fprintf (stdout, "Testing lseek");
        ret = lseek (0, 0, 0);
        check_err (ret, "lseek", 2);

        fprintf (stdout, "Testing dup");
        ret = dup (0);
        check_err (ret, "dup", 2);

        fprintf (stdout, "Testing dup2");
        ret = dup2 (0, 0);
        check_err (ret, "dup2", 2);

        fprintf (stdout, "Testing fchmod");
        ret = fchmod (0, 0);
        check_err (ret, "fchmod", 2);

        fprintf (stdout, "Testing fchown");
        ret = fchown (0, 0, 0);
        check_err (ret, "fchown", 2);

        fprintf (stdout, "Testing fsync");
        ret = fsync (0);
        check_err (ret, "fsync", 2);

        fprintf (stdout, "Testing ftruncate");
        ret = ftruncate (0, 0);
        check_err (ret, "ftruncate", 1);

        fprintf (stdout, "Testing fstat");
        ret = fstat (0, &buf);
        check_err (ret, "fstat", 1);

        fprintf (stdout, "Testing sendfile");
        ret = sendfile (0, 0, NULL, 0);
        check_err (ret, "sendfile", 1);

        fprintf (stdout, "Testing fcntl");
        ret = fcntl (0, 0, NULL);
        check_err (ret, "fcntl", 2);

        fprintf (stdout, "Testing close");
        ret = close (ret);
        check_err (ret, "close", 2);

        fprintf (stdout, "Testing remove");
        ret = remove (testfile);
        check_err (ret, "remove", 2);

        return ret;
}


int
run_attr_tests (char *testfile)
{
        int             ret = -1;
        char            *res = NULL;
        struct stat     buf;
        struct statfs   sbuf;
        struct statvfs  svbuf;

        assert (testfile);

        fprintf (stdout, "Testing chmod");
        ret = chmod (testfile, 0);
        check_err (ret, "chmod", 2);

        fprintf (stdout, "Testing chown");
        ret = chown (testfile, 0, 0);
        check_err (ret, "chown", 2);

        fprintf (stdout, "Testing link");
        ret = link (testfile, testfile);
        check_err (ret, "link", 2);

        fprintf (stdout, "Testing rename");
        ret = rename (testfile, testfile);
        check_err (ret, "rename", 2);

        fprintf (stdout, "Testing utimes");
        ret = utimes (testfile, NULL);
        check_err (ret, "utimes", 2);

        fprintf (stdout, "Testing utime");
        ret = utime (testfile, NULL);
        check_err (ret, "utime", 2);

        fprintf (stdout, "Testing unlink");
        ret = unlink (testfile);
        check_err (ret, "unlink", 2);

        fprintf (stdout, "Testing symlink");
        ret = symlink (testfile, testfile);
        check_err (ret, "symlink", 2);

        fprintf (stdout, "Testing readlink");
        ret = readlink (testfile, testfile, 0);
        check_err (ret, "readlink", 2);

        fprintf (stdout, "Testing realpath");
        ret = 0;
        res = realpath ((const char *)testfile, testfile);
        if (!res)
                ret = -1;
        check_err (ret, "realpath", 2);

        fprintf (stdout, "Testing stat");
        ret = stat (testfile, &buf);
        check_err (ret, "stat", 1);

        fprintf (stdout, "Testing lstat");
        ret = lstat (testfile, &buf);
        check_err (ret, "lstat", 1);

        fprintf (stdout, "Testing statfs");
        ret = statfs (testfile, &sbuf);
        check_err (ret, "statfs", 2);

        fprintf (stdout, "Testing statvfs");
        ret = statvfs (testfile, &svbuf);
        check_err (ret, "statvfs", 1);

        fprintf (stdout, "Testing getxattr");
        ret = getxattr (testfile, NULL, NULL, 0);
        check_err (ret, "getxattr", 2);

        fprintf (stdout, "Testing lgetxattr");
        ret = lgetxattr (testfile, NULL, NULL, 0);
        check_err (ret, "lgetxattr", 1);

        fprintf (stdout, "Testing lchown");
        ret = lchown (testfile, 0, 0);
        check_err (ret, "lchown", 2);
        return 0;
}


int
run_dev_tests (char *testfile)
{
        int     ret = -1;

        assert (testfile);

        fprintf (stdout, "Testing mknod");
        ret = mknod (testfile, 0, 0);
        check_err (ret, "mknod", 2);

        fprintf (stdout, "Testing mkfifo");
        ret = mkfifo (testfile, 0);
        check_err (ret, "mkfifo", 2);
        return 0;
}

int
run_dir_tests (char *testpath)
{
        int             ret = -1;
        DIR             *dh = NULL;
        struct dirent   *dire = NULL;

        assert (testpath);

        fprintf (stdout, "Testing mkdir");
        ret = mkdir (testpath, 0);
        check_err (ret, "mkdir", 2);

        fprintf (stdout, "Testing rmdir");
        ret = rmdir (testpath);
        check_err (ret, "rmdir", 2);

        fprintf (stdout, "Testing opendir");
        ret = 0;
        dh = opendir (testpath);
        if (!dh)
                ret = -1;
        check_err (ret, "opendir", 2);

        fprintf (stdout, "Testing readdir");
        ret = 0;
        dire = readdir (dh);
        if (!dire)
                ret = -1;
        check_err (ret, "readdir", 1);

        fprintf (stdout, "Testing readdir_r");
        ret = readdir_r (dh, dire, &dire);
        check_err (ret, "readdir_r", 1);

        fprintf (stdout, "Testing rewinddir");
        rewinddir (dh);
        check_err (-1, "rewinddir", 1);

        fprintf (stdout, "Testing seekdir");
        seekdir (dh, 0);
        check_err (-1, "seekdir", 2);

        fprintf (stdout, "Testing telldir");
        ret = telldir (dh);
        check_err (ret, "telldir", 2);

        fprintf (stdout, "Testing closedir");
        ret = closedir (dh);
        check_err (ret, "closedir", 2);
        return 0;
}



int
main (int argc, char *argv[])
{
        char            *testpath = NULL;
        int             x = 0;

        for (;x < argc; ++x) {
                if (strcmp (argv[x], "--path") == 0) {
                        testpath = argv[x+1];
                        continue;
                }

        }

        if (!testpath) {
                fprintf (stderr, "--path not specified\n");
                usage (stderr);
                return -1;
        }

        run_file_tests (testpath);
        run_dir_tests (testpath);
        run_attr_tests (testpath);
        run_dev_tests (testpath);

        return 0;
}


