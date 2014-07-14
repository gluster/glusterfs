#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <attr/xattr.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

int fd_based_fops_1 (char *filename); //for fd based fops after unlink
int fd_based_fops_2 (char *filename); //for fd based fops before unlink
int dup_fd_based_fops (char *filename); // fops based on fd after dup
int path_based_fops (char *filename); //for fops based on path
int dir_based_fops (char *filename); // for fops which operate on directory
int link_based_fops (char *filename); //for fops which operate in link files (symlinks)
int test_open_modes (char *filename); // to test open syscall with open modes available.
int generic_open_read_write (char *filename, int flag); // generic function which does open write and read.

int
main (int argc, char *argv[])
{
        int   ret           = -1;
        char  filename[255] = {0,};

        if (argc > 1)
                strcpy(filename, argv[1]);
        else
                strcpy(filename, "temp-xattr-test-file");

        ret = fd_based_fops_1 (strcat(filename, "_1"));
        if (ret < 0)
                fprintf (stderr, "fd based file operation 1 failed\n");
        else
                fprintf (stdout, "fd based file operation 1 passed\n");

        ret = fd_based_fops_2 (strcat(filename, "_2"));
        if (ret < 0)
                fprintf (stderr, "fd based file operation 2 failed\n");
        else
                fprintf (stdout, "fd based file operation 2 passed\n");

        ret = dup_fd_based_fops (strcat (filename, "_3"));
        if (ret < 0)
                fprintf (stderr, "dup fd based file operation failed\n");
        else
                fprintf (stdout, "dup fd based file operation passed\n");

        ret = path_based_fops (strcat (filename, "_4"));
        if (ret < 0)
                fprintf (stderr, "path based file operation failed\n");
        else
                fprintf (stdout, "path based file operation passed\n");

        ret = dir_based_fops (strcat (filename, "_5"));
        if (ret < 0)
                fprintf (stderr, "directory based file operation failed\n");
        else
                fprintf (stdout, "directory based file operation passed\n");

        ret = link_based_fops (strcat (filename, "_5"));
        if (ret < 0)
                fprintf (stderr, "link based file operation failed\n");
        else
                fprintf (stdout, "link based file operation passed\n");

        ret = test_open_modes (strcat (filename, "_5"));
        if (ret < 0)
                fprintf (stderr, "testing modes of 'open' call failed\n");
        else
                fprintf (stdout, "testing modes of 'open' call passed\n");

out:
        return ret;
}

int
fd_based_fops_1 (char *filename)
{
        int         fd        = 0;
        int         ret       = -1;
        struct stat stbuf     = {0,};
        char        wstr[50]  = {0,};
        char        rstr[50]  = {0,};

        fd = open (filename, O_RDWR|O_CREAT);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                goto out;
        }

        ret = unlink (filename);
        if (ret < 0) {
                fprintf (stderr, "unlink failed : %s\n", strerror (errno));
                goto out;
        }

        strcpy (wstr, "This is my string\n");
        ret = write (fd, wstr, strlen(wstr));
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "write failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lseek (fd, 0, SEEK_SET);
        if (ret < 0) {
                fprintf (stderr, "lseek failed: %s\n", strerror (errno));
                goto out;
        }

        ret = read (fd, rstr, strlen(wstr));
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "read failed: %s\n", strerror (errno));
                goto out;
        }

        ret = memcmp (rstr, wstr, strlen (wstr));
        if (ret != 0) {
                ret = -1;
                fprintf (stderr, "read returning junk\n");
                goto out;
        }

        ret = ftruncate (fd, 0);
        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fstat (fd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchmod (fd, 0640);
        if (ret < 0) {
                fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchown (fd, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "fchown failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsync (fd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsetxattr (fd, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fdatasync (fd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = flistxattr (fd, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fgetxattr (fd, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fremovexattr (fd, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = 0;
out:
        if (fd)
                close (fd);

        return ret;
}


int
fd_based_fops_2 (char *filename)
{
        int         fd    = 0;
        int         ret   = -1;
        struct stat stbuf = {0,};
        char        wstr[50]  = {0,};
        char        rstr[50]  = {0,};

        fd = open (filename, O_RDWR|O_CREAT);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                goto out;
        }

        ret = ftruncate (fd, 0);

        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                goto out;
        }

        strcpy (wstr, "This is my second string\n");
        ret = write (fd, wstr, strlen (wstr));
        if (ret < 0) {
                ret = -1;
                fprintf (stderr, "write failed: %s\n", strerror (errno));
                goto out;
        }

        lseek (fd, 0, SEEK_SET);
        if (ret < 0) {
                fprintf (stderr, "lseek failed: %s\n", strerror (errno));
                goto out;
        }

        ret = read (fd, rstr, strlen (wstr));
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "read failed: %s\n", strerror (errno));
                goto out;
        }

        ret = memcmp (rstr, wstr, strlen (wstr));
        if (ret != 0) {
                ret = -1;
                fprintf (stderr, "read returning junk\n");
                goto out;
        }

        ret = fstat (fd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchmod (fd, 0640);
        if (ret < 0) {
                fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchown (fd, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "fchown failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsync (fd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsetxattr (fd, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fdatasync (fd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = flistxattr (fd, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fgetxattr (fd, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fremovexattr (fd, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
                goto out;
        }

out:
        if (fd)
                close (fd);
        unlink (filename);

        return ret;
}

int
path_based_fops (char *filename)
{
        int         ret              = -1;
        int         fd               = 0;
        struct stat stbuf            = {0,};
        char        newfilename[255] = {0,};

        fd = creat (filename, 0644);
        if (fd < 0) {
                fprintf (stderr, "creat failed: %s\n", strerror (errno));
                goto out;
        }

        ret = truncate (filename, 0);
        if (ret < 0) {
                fprintf (stderr, "truncate failed: %s\n", strerror (errno));
                goto out;
        }

        ret = stat (filename, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "stat failed: %s\n", strerror (errno));
                goto out;
        }

        ret = chmod (filename, 0640);
        if (ret < 0) {
                fprintf (stderr, "chmod failed: %s\n", strerror (errno));
                goto out;
        }

        ret = chown (filename, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "chown failed: %s\n", strerror (errno));
                goto out;
        }

        ret = setxattr (filename, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "setxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = listxattr (filename, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "listxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = getxattr (filename, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "getxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = removexattr (filename, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "removexattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = access (filename, R_OK|W_OK);
        if (ret < 0) {
                fprintf (stderr, "access failed: %s\n", strerror (errno));
                goto out;
        }

        strcpy (newfilename, filename);
        strcat(newfilename, "_new");
        ret = rename (filename, newfilename);
        if (ret < 0) {
                fprintf (stderr, "rename failed: %s\n", strerror (errno));
                goto out;
        }
        unlink (newfilename);

out:
        if (fd)
                close (fd);

        unlink (filename);
        return ret;
}

int
dup_fd_based_fops (char *filename)
{
        int         fd        = 0;
        int         newfd     = 0;
        int         ret       = -1;
        struct stat stbuf     = {0,};
        char        wstr[50]  = {0,};
        char        rstr[50]  = {0,};

        fd = open (filename, O_RDWR|O_CREAT);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                goto out;
        }

        newfd = dup (fd);
        if (newfd < 0) {
                ret = -1;
                fprintf (stderr, "dup failed: %s\n", strerror (errno));
                goto out;
        }

        close (fd);

        strcpy (wstr, "This is my string\n");
        ret = write (newfd, wstr, strlen(wstr));
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "write failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lseek (newfd, 0, SEEK_SET);
        if (ret < 0) {
                fprintf (stderr, "lseek failed: %s\n", strerror (errno));
                goto out;
        }

        ret = read (newfd, rstr, strlen(wstr));
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "read failed: %s\n", strerror (errno));
                goto out;
        }

        ret = memcmp (rstr, wstr, strlen (wstr));
        if (ret != 0) {
                ret = -1;
                fprintf (stderr, "read returning junk\n");
                goto out;
        }

        ret = ftruncate (newfd, 0);
        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fstat (newfd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchmod (newfd, 0640);
        if (ret < 0) {
                fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchown (newfd, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "fchown failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsync (newfd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsetxattr (newfd, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fdatasync (newfd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = flistxattr (newfd, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fgetxattr (newfd, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fremovexattr (newfd, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = 0;
out:
        if (newfd)
                close (newfd);
        ret = unlink (filename);
        if (ret < 0)
                fprintf (stderr, "unlink failed : %s\n", strerror (errno));

        return ret;
}

int
dir_based_fops (char *dirname)
{
        int            ret           = -1;
        DIR           *dp            = NULL;
        char           buff[255]     = {0,};
        struct dirent *dbuff         = {0,};
        struct stat    stbuff        = {0,};
        char           newdname[255] = {0,};
        char          *cwd           = NULL;

        ret = mkdir (dirname, 0755);
        if (ret < 0) {
                fprintf (stderr, "mkdir failed: %s\n", strerror (errno));
                goto out;
        }

        dp = opendir (dirname);
        if (dp == NULL) {
                fprintf (stderr, "opendir failed: %s\n", strerror (errno));
                goto out;
        }

        dbuff = readdir (dp);
        if (NULL == dbuff) {
                fprintf (stderr, "readdir failed: %s\n", strerror (errno));
                goto out;
        }

        ret = closedir (dp);
        if (ret < 0) {
                fprintf (stderr, "closedir failed: %s\n", strerror (errno));
                goto out;
        }

        ret = stat (dirname, &stbuff);
        if (ret < 0) {
                fprintf (stderr, "stat failed: %s\n", strerror (errno));
                goto out;
        }

        ret = chmod (dirname, 0744);
        if (ret < 0) {
                fprintf (stderr, "chmod failed: %s\n", strerror (errno));
                goto out;
        }

        ret = chown (dirname, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "chmod failed: %s\n", strerror (errno));
                goto out;
        }

        ret = setxattr (dirname, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "setxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = listxattr (dirname, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "listxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = getxattr (dirname, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "getxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = removexattr (dirname, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "removexattr failed: %s\n", strerror (errno));
                goto out;
        }

        strcpy (newdname, dirname);
        strcat (newdname, "/../");
        ret = chdir (newdname);
        if (ret < 0) {
                fprintf (stderr, "chdir failed: %s\n", strerror (errno));
                goto out;
        }

        cwd = getcwd (buff, 255);
        if (NULL == cwd) {
                fprintf (stderr, "getcwd failed: %s\n", strerror (errno));
                goto out;
        }

        strcpy (newdname, dirname);
        strcat (newdname, "new");
        ret = rename (dirname, newdname);
        if (ret < 0) {
                fprintf (stderr, "rename failed: %s\n", strerror (errno));
                goto out;
        }

        ret = rmdir (newdname);
        if (ret < 0) {
                fprintf (stderr, "rmdir failed: %s\n", strerror (errno));
                return ret;
        }

out:
        rmdir (dirname);
        return ret;
}

int
link_based_fops (char *filename)
{
        int         ret           = -1;
        int         fd            = 0;
        char        newname[255]  = {0,};
        char        linkname[255] = {0,};
        struct stat lstbuf        = {0,};

        fd = creat (filename, 0644);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "creat failed: %s\n", strerror (errno));
                goto out;
        }

        strcpy (newname, filename);
        strcat (newname, "_hlink");
        ret = link (filename, newname);
        if (ret < 0) {
                fprintf (stderr, "link failed: %s\n", strerror (errno));
                goto out;
        }

        ret = unlink (filename);
        if (ret < 0) {
                fprintf (stderr, "unlink failed: %s\n", strerror (errno));
                goto out;
        }

        strcpy (linkname, filename);
        strcat (linkname, "_slink");
        ret = symlink (newname, linkname);
        if (ret < 0) {
                fprintf (stderr, "symlink failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lstat (linkname, &lstbuf);
        if (ret < 0) {
                fprintf (stderr, "lstbuf failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lchown (linkname, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "lchown failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lsetxattr (linkname, "trusted.lxattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "lsetxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = llistxattr (linkname, NULL, 0);
        if (ret < 0) {
                ret = -1;
                fprintf (stderr, "llistxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lgetxattr (linkname, "trusted.lxattr-test", NULL, 0);
        if (ret < 0) {
                ret = -1;
                fprintf (stderr, "lgetxattr failed: %s\n", strerror (errno));
                goto out;
        }

        ret = lremovexattr (linkname, "trusted.lxattr-test");
        if (ret < 0) {
                fprintf (stderr, "lremovexattr failed: %s\n", strerror (errno));
                goto out;
        }


out:
        if (fd)
                close(fd);
        unlink (linkname);
        unlink (newname);
}

int
test_open_modes (char *filename)
{
        int ret = -1;

        ret = generic_open_read_write (filename, O_CREAT|O_WRONLY);
        if (3 != ret) {
               fprintf (stderr, "flag O_CREAT|O_WRONLY failed: \n");
               goto out;
        }

        ret = generic_open_read_write (filename, O_CREAT|O_RDWR);
        if (ret != 0) {
               fprintf (stderr, "flag O_CREAT|O_RDWR failed\n");
               goto out;
        }

        ret = generic_open_read_write (filename, O_CREAT|O_RDONLY);
        if (ret != 0) {
                fprintf (stderr, "flag O_CREAT|O_RDONLY failed\n");
                goto out;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_WRONLY);
        if (3 != ret) {
               fprintf (stderr, "flag O_WRONLY failed\n");
               goto out;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_RDWR);
        if (0 != ret) {
               fprintf (stderr, "flag O_RDWR failed\n");
               goto out;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_RDONLY);
        if (0 != ret) {
               fprintf (stderr, "flag O_RDONLY failed\n");
               goto out;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_TRUNC|O_WRONLY);
        if (3 != ret) {
               fprintf (stderr, "flag O_TRUNC|O_WRONLY failed\n");
               goto out;
        }

#if 0 /* undefined behaviour, unable to reliably test */
        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_TRUNC|O_RDONLY);
        if (0 != ret) {
               fprintf (stderr, "flag O_TRUNC|O_RDONLY failed\n");
               goto out;
        }
#endif

        ret = generic_open_read_write (filename, O_CREAT|O_RDWR|O_SYNC);
        if (0 != ret) {
               fprintf (stderr, "flag O_CREAT|O_RDWR|O_SYNC failed\n");
               goto out;
        }

        ret = creat (filename, 0644);
        close (ret);
        ret = generic_open_read_write (filename, O_CREAT|O_EXCL);
        if (0 != ret) {
                fprintf (stderr, "flag O_CREAT|O_EXCL failed\n");
                goto out;
        }

out:
        return ret;
}

int generic_open_read_write (char *filename, int flag)
{
        int  fd          = 0;
        int  ret         = -1;
        char wstring[50] = {0,};
        char rstring[50] = {0,};

        fd = open (filename, flag);
        if (fd < 0) {
                if (flag == O_CREAT|O_EXCL && errno == EEXIST) {
                        unlink (filename);
                        return 0;
                }
                else {
                        fd = 0;
                        fprintf (stderr, "open failed: %s\n", strerror (errno));
                        return 1;
                }
        }

        strcpy (wstring, "My string to write\n");
        ret = write (fd, wstring, strlen(wstring));
        if (ret <= 0) {
                if (errno != EBADF) {
                        fprintf (stderr, "write failed: %s\n", strerror (errno));
                        close (fd);
                        unlink(filename);
                        return 2;
                }
        }

        ret = lseek (fd, 0, SEEK_SET);
        if (ret < 0) {
                close (fd);
                unlink(filename);
                return 4;
        }

        ret = read (fd, rstring, strlen(wstring));
        if (ret < 0) {
                close (fd);
                unlink (filename);
                return 3;
        }

        /* Compare the rstring with wstring. But we do not want to return
         * error when the flag is either O_RDONLY, O_CREAT|O_RDONLY or
         * O_TRUNC|O_RDONLY. Because in that case we are not writing
         * anything to the file.*/

        ret = memcmp (wstring, rstring, strlen (wstring));
        if (0 != ret  && !(flag == O_CREAT|O_RDONLY || flag == O_RDONLY ||\
            flag == O_TRUNC|O_RDONLY)) {
                fprintf (stderr, "read is returning junk\n");
                close (fd);
                unlink (filename);
                return 4;
        }

        close (fd);
        unlink (filename);
        return 0;
}
