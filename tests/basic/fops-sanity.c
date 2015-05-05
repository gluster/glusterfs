/* Copyright (c) 2014 Red Hat, Inc. All rights reserved.

* This copyrighted material is made available to anyone wishing
* to use, modify, copy, or redistribute it subject to the terms
* and conditions of the GNU General Public License version 2.

* This program is distributed in the hope that it will be
* useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
* PURPOSE. See the GNU General Public License for more details.

* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the Free
* Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301, USA.
*/

/* Filesystem basic sanity check, tests all (almost) fops. */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#ifndef linux
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#endif

/* for fd based fops after unlink */
int fd_based_fops_1 (char *filename);
/* for fd based fops before unlink */
int fd_based_fops_2 (char *filename);
/* fops based on fd after dup */
int dup_fd_based_fops (char *filename);
/* for fops based on path */
int path_based_fops (char *filename);
/* for fops which operate on directory */
int dir_based_fops (char *filename);
/* for fops which operate in link files (symlinks) */
int link_based_fops (char *filename);
/* to test open syscall with open modes available. */
int test_open_modes (char *filename);
/* generic function which does open write and read. */
int generic_open_read_write (char *filename, int flag, mode_t mode);

#define OPEN_MODE   0666

int
main (int argc, char *argv[])
{
        int   ret           = -1;
        int   result        = 0;
        char  filename[255] = {0,};

        if (argc > 1)
                strcpy(filename, argv[1]);
        else
                strcpy(filename, "temp-xattr-test-file");

        ret = fd_based_fops_1 (strcat(filename, "_1"));
        if (ret < 0) {
                fprintf (stderr, "fd based file operation 1 failed\n");
                result |= ret;
        } else {
                fprintf (stdout, "fd based file operation 1 passed\n");
        }

        ret = fd_based_fops_2 (strcat(filename, "_2"));
        if (ret < 0) {
                result |= ret;
                fprintf (stderr, "fd based file operation 2 failed\n");
        } else {
                fprintf (stdout, "fd based file operation 2 passed\n");
        }

        ret = dup_fd_based_fops (strcat (filename, "_3"));
        if (ret < 0) {
                result |= ret;
                fprintf (stderr, "dup fd based file operation failed\n");
        } else {
                fprintf (stdout, "dup fd based file operation passed\n");
        }

        ret = path_based_fops (strcat (filename, "_4"));
        if (ret < 0) {
                result |= ret;
                fprintf (stderr, "path based file operation failed\n");
        } else {
                fprintf (stdout, "path based file operation passed\n");
        }

        ret = dir_based_fops (strcat (filename, "_5"));
        if (ret < 0) {
                result |= ret;
                fprintf (stderr, "directory based file operation failed\n");
        } else {
                fprintf (stdout, "directory based file operation passed\n");
        }

        ret = link_based_fops (strcat (filename, "_5"));
        if (ret < 0) {
                result |= ret;
                fprintf (stderr, "link based file operation failed\n");
        } else {
                fprintf (stdout, "link based file operation passed\n");
        }

        ret = test_open_modes (strcat (filename, "_5"));
        if (ret < 0) {
                result |= ret;
                fprintf (stderr, "testing modes of `open' call failed\n");
        } else {
                fprintf (stdout, "testing modes of `open' call passed\n");
        }
        return result;
}

/* Execute all possible fops on a fd which is unlinked */
int
fd_based_fops_1 (char *filename)
{
        int         fd        = 0;
        int         ret       = -1;
        int         result    = 0;
        struct stat stbuf     = {0,};
        char        wstr[50]  = {0,};
        char        rstr[50]  = {0,};

        fd = open (filename, O_RDWR|O_CREAT, OPEN_MODE);
        if (fd < 0) {
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                return ret;
        }

        ret = unlink (filename);
        if (ret < 0) {
                fprintf (stderr, "unlink failed : %s\n", strerror (errno));
                result |= ret;
        }

        strcpy (wstr, "This is my string\n");
        ret = write (fd, wstr, strlen(wstr));
        if (ret <= 0) {
                fprintf (stderr, "write failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lseek (fd, 0, SEEK_SET);
        if (ret < 0) {
                fprintf (stderr, "lseek failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = read (fd, rstr, strlen(wstr));
        if (ret <= 0) {
                fprintf (stderr, "read failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = memcmp (rstr, wstr, strlen (wstr));
        if (ret != 0) {
                fprintf (stderr, "read returning junk\n");
                result |= ret;
        }

        ret = ftruncate (fd, 0);
        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fstat (fd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fsync (fd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fdatasync (fd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                result |= ret;
        }

/*
 *      These metadata operations fail at the moment because kernel doesn't
 *      pass the client fd in the operation.
 *      The following bug tracks this change.
 *      https://bugzilla.redhat.com/show_bug.cgi?id=1084422
 *      ret = fchmod (fd, 0640);
 *      if (ret < 0) {
 *              fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
 *              result |= ret;
 *      }

 *      ret = fchown (fd, 10001, 10001);
 *      if (ret < 0) {
 *              fprintf (stderr, "fchown failed : %s\n", strerror (errno));
 *              result |= ret;
 *      }

 *      ret = fsetxattr (fd, "trusted.xattr-test", "working", 8, 0);
 *      if (ret < 0) {
 *              fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
 *              result |= ret;
 *      }

 *      ret = flistxattr (fd, NULL, 0);
 *      if (ret <= 0) {
 *              fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
 *              result |= ret;
 *      }

 *      ret = fgetxattr (fd, "trusted.xattr-test", NULL, 0);
 *      if (ret <= 0) {
 *              fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
 *              result |= ret;
 *      }

 *      ret = fremovexattr (fd, "trusted.xattr-test");
 *      if (ret < 0) {
 *              fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
 *              result |= ret;
 *      }
 */

        if (fd)
                close(fd);
        return result;
}


int
fd_based_fops_2 (char *filename)
{
        int     fd              = 0;
        int     ret             = -1;
        int     result          = 0;
        struct stat stbuf       = {0,};
        char        wstr[50]    = {0,};
        char        rstr[50]    = {0,};

        fd = open (filename, O_RDWR|O_CREAT, OPEN_MODE);
        if (fd < 0) {
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                return ret;
        }

        ret = ftruncate (fd, 0);
        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                result |= ret;
        }

        strcpy (wstr, "This is my second string\n");
        ret = write (fd, wstr, strlen (wstr));
        if (ret < 0) {
                fprintf (stderr, "write failed: %s\n", strerror (errno));
                result |= ret;
        }

        lseek (fd, 0, SEEK_SET);
        if (ret < 0) {
                fprintf (stderr, "lseek failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = read (fd, rstr, strlen (wstr));
        if (ret <= 0) {
                fprintf (stderr, "read failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = memcmp (rstr, wstr, strlen (wstr));
        if (ret != 0) {
                fprintf (stderr, "read returning junk\n");
                result |= ret;
        }

        ret = fstat (fd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fchmod (fd, 0640);
        if (ret < 0) {
                fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fchown (fd, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "fchown failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fsync (fd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fsetxattr (fd, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fdatasync (fd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = flistxattr (fd, NULL, 0);
        if (ret <= 0) {
                fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fgetxattr (fd, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fremovexattr (fd, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        if (fd)
                close (fd);
        unlink (filename);

        return result;
}

int
path_based_fops (char *filename)
{
        int         ret              = -1;
        int         fd               = 0;
        int         result           = 0;
        struct stat stbuf            = {0,};
        char        newfilename[255] = {0,};
        char        *hardlink        = "linkfile-hard.txt";
        char        *symlnk          = "linkfile-soft.txt";
        char        buf[1024]        = {0,};

        fd = creat (filename, 0644);
        if (fd < 0) {
                fprintf (stderr, "creat failed: %s\n", strerror (errno));
                return ret;
        }

        ret = truncate (filename, 0);
        if (ret < 0) {
                fprintf (stderr, "truncate failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = stat (filename, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "stat failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = chmod (filename, 0640);
        if (ret < 0) {
                fprintf (stderr, "chmod failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = chown (filename, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "chown failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = setxattr (filename, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "setxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = listxattr (filename, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "listxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = getxattr (filename, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                fprintf (stderr, "getxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = removexattr (filename, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "removexattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = access (filename, R_OK|W_OK);
        if (ret < 0) {
                fprintf (stderr, "access failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = link (filename, hardlink);
        if (ret < 0) {
                fprintf (stderr, "link failed: %s\n", strerror(errno));
                result |= ret;
        }
        unlink(hardlink);

        ret = symlink (filename, symlnk);
        if (ret < 0) {
                fprintf (stderr, "symlink failed: %s\n", strerror(errno));
                result |= ret;
        }

        ret = readlink (symlnk, buf, sizeof(buf));
        if (ret < 0) {
                fprintf (stderr, "readlink failed: %s\n", strerror(errno));
                result |= ret;
        }
        unlink(symlnk);

        /* Create a character special file */
        ret = mknod ("cspecial", S_IFCHR|S_IRWXU|S_IRWXG, makedev(2,3));
        if (ret < 0) {
                fprintf (stderr, "cpsecial mknod failed: %s\n",
                         strerror(errno));
                result |= ret;
        }
        unlink("cspecial");

        ret = mknod ("bspecial", S_IFBLK|S_IRWXU|S_IRWXG, makedev(4,5));
        if (ret < 0) {
                fprintf (stderr, "bspecial mknod failed: %s\n",
                         strerror(errno));
                result |= ret;
        }
        unlink("bspecial");

#ifdef linux
        ret = mknod ("fifo", S_IFIFO|S_IRWXU|S_IRWXG, 0);
#else
        ret = mkfifo ("fifo", 0);
#endif
        if (ret < 0) {
                fprintf (stderr, "fifo mknod failed: %s\n",
                         strerror(errno));
                result |= ret;
        }
        unlink("fifo");

#ifdef linux
        ret = mknod ("sock", S_IFSOCK|S_IRWXU|S_IRWXG, 0);
        if (ret < 0) {
                fprintf (stderr, "sock mknod failed: %s\n",
                         strerror(errno));
                result |= ret;
        }
#else
        {
                int s;
                const char *pathname = "sock";
                struct sockaddr_un addr;

                s = socket(PF_LOCAL, SOCK_STREAM, 0);
                memset(&addr, 0, sizeof(addr));
                strncpy(addr.sun_path, pathname, sizeof(addr.sun_path));
                ret = bind(s, (const struct sockaddr *)&addr, SUN_LEN(&addr));
                if (ret < 0) {
                        fprintf (stderr, "fifo mknod failed: %s\n",
                                 strerror(errno));
                        result |= ret;
                }
                close(s);
        }
#endif
        unlink("sock");

        strcpy (newfilename, filename);
        strcat(newfilename, "_new");
        ret = rename (filename, newfilename);
        if (ret < 0) {
                fprintf (stderr, "rename failed: %s\n", strerror (errno));
                result |= ret;
        }
        unlink (newfilename);

        if (fd)
                close (fd);

        unlink (filename);
        return result;
}

int
dup_fd_based_fops (char *filename)
{
        int         fd        = 0;
        int         result    = 0;
        int         newfd     = 0;
        int         ret       = -1;
        struct stat stbuf     = {0,};
        char        wstr[50]  = {0,};
        char        rstr[50]  = {0,};

        fd = open (filename, O_RDWR|O_CREAT, OPEN_MODE);
        if (fd < 0) {
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                return ret;
        }

        newfd = dup (fd);
        if (newfd < 0) {
                fprintf (stderr, "dup failed: %s\n", strerror (errno));
                result |= ret;
        }

        close (fd);

        strcpy (wstr, "This is my string\n");
        ret = write (newfd, wstr, strlen(wstr));
        if (ret <= 0) {
                fprintf (stderr, "write failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lseek (newfd, 0, SEEK_SET);
        if (ret < 0) {
                fprintf (stderr, "lseek failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = read (newfd, rstr, strlen(wstr));
        if (ret <= 0) {
                fprintf (stderr, "read failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = memcmp (rstr, wstr, strlen (wstr));
        if (ret != 0) {
                fprintf (stderr, "read returning junk\n");
                result |= ret;
        }

        ret = ftruncate (newfd, 0);
        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fstat (newfd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fchmod (newfd, 0640);
        if (ret < 0) {
                fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fchown (newfd, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "fchown failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fsync (newfd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fsetxattr (newfd, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fdatasync (newfd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = flistxattr (newfd, NULL, 0);
        if (ret <= 0) {
                fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fgetxattr (newfd, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        ret = fremovexattr (newfd, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
                result |= ret;
        }

        if (newfd)
                close (newfd);
        ret = unlink (filename);
        if (ret < 0) {
                fprintf (stderr, "unlink failed : %s\n", strerror (errno));
                result |= ret;
        }
        return result;
}

int
dir_based_fops (char *dirname)
{
        int            ret           = -1;
        int            result        = 0;
        DIR           *dp            = NULL;
        char           buff[255]     = {0,};
        struct dirent *dbuff         = {0,};
        struct stat    stbuff        = {0,};
        char           newdname[255] = {0,};
        char          *cwd           = NULL;

        ret = mkdir (dirname, 0755);
        if (ret < 0) {
                fprintf (stderr, "mkdir failed: %s\n", strerror (errno));
                return ret;
        }

        dp = opendir (dirname);
        if (dp == NULL) {
                fprintf (stderr, "opendir failed: %s\n", strerror (errno));
                result |= ret;
        }

        dbuff = readdir (dp);
        if (NULL == dbuff) {
                fprintf (stderr, "readdir failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = closedir (dp);
        if (ret < 0) {
                fprintf (stderr, "closedir failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = stat (dirname, &stbuff);
        if (ret < 0) {
                fprintf (stderr, "stat failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = chmod (dirname, 0744);
        if (ret < 0) {
                fprintf (stderr, "chmod failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = chown (dirname, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "chmod failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = setxattr (dirname, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "setxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = listxattr (dirname, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "listxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = getxattr (dirname, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "getxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = removexattr (dirname, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "removexattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        strcpy (newdname, dirname);
        strcat (newdname, "/../");
        ret = chdir (newdname);
        if (ret < 0) {
                fprintf (stderr, "chdir failed: %s\n", strerror (errno));
                result |= ret;
        }

        cwd = getcwd (buff, 255);
        if (NULL == cwd) {
                fprintf (stderr, "getcwd failed: %s\n", strerror (errno));
                result |= ret;
        }

        strcpy (newdname, dirname);
        strcat (newdname, "new");
        ret = rename (dirname, newdname);
        if (ret < 0) {
                fprintf (stderr, "rename failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = rmdir (newdname);
        if (ret < 0) {
                fprintf (stderr, "rmdir failed: %s\n", strerror (errno));
                result |= ret;
        }

        rmdir (dirname);
        return result;
}

int
link_based_fops (char *filename)
{
        int         ret           = -1;
        int         result        = 0;
        int         fd            = 0;
        char        newname[255]  = {0,};
        char        linkname[255] = {0,};
        struct stat lstbuf        = {0,};

        fd = creat (filename, 0644);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "creat failed: %s\n", strerror (errno));
                return ret;
        }

        strcpy (newname, filename);
        strcat (newname, "_hlink");
        ret = link (filename, newname);
        if (ret < 0) {
                fprintf (stderr, "link failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = unlink (filename);
        if (ret < 0) {
                fprintf (stderr, "unlink failed: %s\n", strerror (errno));
                result |= ret;
        }

        strcpy (linkname, filename);
        strcat (linkname, "_slink");
        ret = symlink (newname, linkname);
        if (ret < 0) {
                fprintf (stderr, "symlink failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lstat (linkname, &lstbuf);
        if (ret < 0) {
                fprintf (stderr, "lstbuf failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lchown (linkname, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "lchown failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lsetxattr (linkname, "trusted.lxattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "lsetxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = llistxattr (linkname, NULL, 0);
        if (ret < 0) {
                ret = -1;
                fprintf (stderr, "llistxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lgetxattr (linkname, "trusted.lxattr-test", NULL, 0);
        if (ret < 0) {
                ret = -1;
                fprintf (stderr, "lgetxattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        ret = lremovexattr (linkname, "trusted.lxattr-test");
        if (ret < 0) {
                fprintf (stderr, "lremovexattr failed: %s\n", strerror (errno));
                result |= ret;
        }

        if (fd)
                close(fd);
        unlink (linkname);
        unlink (newname);
        return result;
}

int
test_open_modes (char *filename)
{
        int ret         = -1;
        int result      = 0;

        ret = generic_open_read_write (filename, O_CREAT|O_WRONLY, OPEN_MODE);
        if (ret != 0) {
               fprintf (stderr, "flag O_CREAT|O_WRONLY failed: \n");
               result |= ret;
        }

        ret = generic_open_read_write (filename, O_CREAT|O_RDWR, OPEN_MODE);
        if (ret != 0) {
               fprintf (stderr, "flag O_CREAT|O_RDWR failed\n");
               result |= ret;
        }

        ret = generic_open_read_write (filename, O_CREAT|O_RDONLY, OPEN_MODE);
        if (ret != 0) {
                fprintf (stderr, "flag O_CREAT|O_RDONLY failed\n");
                result |= ret;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_WRONLY, 0);
        if (ret != 0) {
               fprintf (stderr, "flag O_WRONLY failed\n");
               result |= ret;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_RDWR, 0);
        if (0 != ret) {
               fprintf (stderr, "flag O_RDWR failed\n");
               result |= ret;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_RDONLY, 0);
        if (0 != ret) {
               fprintf (stderr, "flag O_RDONLY failed\n");
               result |= ret;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_TRUNC|O_WRONLY, 0);
        if (0 != ret) {
               fprintf (stderr, "flag O_TRUNC|O_WRONLY failed\n");
               result |= ret;
        }

#if 0 /* undefined behaviour, unable to reliably test */
        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_TRUNC|O_RDONLY, 0);
        if (0 != ret) {
               fprintf (stderr, "flag O_TRUNC|O_RDONLY failed\n");
               result |= ret;
        }
#endif

        ret = generic_open_read_write (filename, O_CREAT|O_RDWR|O_SYNC,
                                       OPEN_MODE);
        if (0 != ret) {
               fprintf (stderr, "flag O_CREAT|O_RDWR|O_SYNC failed\n");
               result |= ret;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_CREAT|O_EXCL, OPEN_MODE);
        if (0 != ret) {
                fprintf (stderr, "flag O_CREAT|O_EXCL failed\n");
                result |= ret;
        }

        return result;
}

int
generic_open_read_write (char *filename, int flag, mode_t mode)
{
        int  fd          = 0;
        int  ret         = -1;
        char wstring[50] = {0,};
        char rstring[50] = {0,};

        fd = open (filename, flag, mode);
        if (fd < 0) {
                if (flag == (O_CREAT|O_EXCL) && errno == EEXIST) {
                        unlink (filename);
                        return 0;
                }
                else {
                        fprintf (stderr, "open failed: %s\n", strerror (errno));
                        return -1;
                }
        }

        strcpy (wstring, "My string to write\n");
        ret = write (fd, wstring, strlen(wstring));
        if (ret <= 0) {
                if (errno != EBADF) {
                        fprintf (stderr, "write failed: %s\n", strerror (errno));
                        close (fd);
                        unlink(filename);
                        return ret;
                }
        }

        ret = lseek (fd, 0, SEEK_SET);
        if (ret < 0) {
                close (fd);
                unlink(filename);
                return ret;
        }

        ret = read (fd, rstring, strlen(wstring));
        if (ret < 0 && flag != (O_CREAT|O_WRONLY) && flag != O_WRONLY && \
            flag != (O_TRUNC|O_WRONLY)) {
                close (fd);
                unlink (filename);
                return ret;
        }

        /* Compare the rstring with wstring. But we do not want to return
         * error when the flag is either O_RDONLY, O_CREAT|O_RDONLY or
         * O_TRUNC|O_RDONLY. Because in that case we are not writing
         * anything to the file.*/

        ret = memcmp (wstring, rstring, strlen (wstring));
        if (0 != ret && flag != (O_TRUNC|O_WRONLY) && flag != O_WRONLY && \
                                 flag != (O_CREAT|O_WRONLY) && !(flag == \
                                 (O_CREAT|O_RDONLY) || flag == O_RDONLY \
                                  || flag == (O_TRUNC|O_RDONLY))) {
                fprintf (stderr, "read is returning junk\n");
                close (fd);
                unlink (filename);
                return ret;
        }

        close (fd);
        unlink (filename);
        return 0;
}
