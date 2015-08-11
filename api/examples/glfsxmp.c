#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "api/glfs.h"
#include "api/glfs-handles.h"
#include <string.h>
#include <time.h>


int
test_dirops (glfs_t *fs)
{
        glfs_fd_t *fd = NULL;
        char buf[512];
        struct dirent *entry = NULL;

        fd = glfs_opendir (fs, "/");
        if (!fd) {
                fprintf (stderr, "/: %s\n", strerror (errno));
                return -1;
        }

        fprintf (stderr, "Entries:\n");
        while (glfs_readdir_r (fd, (struct dirent *)buf, &entry), entry) {
                fprintf (stderr, "%s: %lu\n", entry->d_name, glfs_telldir (fd));
        }

        glfs_closedir (fd);
        return 0;
}


int
test_xattr (glfs_t *fs)
{
        char *filename = "/filename2";
        char buf[512];
        char *ptr;
        int ret;

        ret = glfs_setxattr (fs, filename, "user.testkey", "testval", 8, 0);
        fprintf (stderr, "setxattr(%s): %d (%s)\n", filename, ret,
                 strerror (errno));

        ret = glfs_setxattr (fs, filename, "user.testkey2", "testval", 8, 0);
        fprintf (stderr, "setxattr(%s): %d (%s)\n", filename, ret,
                 strerror (errno));

        ret = glfs_listxattr (fs, filename, buf, 512);
        fprintf (stderr, "listxattr(%s): %d (%s)\n", filename, ret,
                 strerror (errno));
        if (ret < 0)
                return -1;

        for (ptr = buf; ptr < buf + ret; ptr++) {
                printf ("key=%s\n", ptr);
                ptr += strlen (ptr);
        }

        return 0;
}


int
test_chdir (glfs_t *fs)
{
        int ret = -1;
        char *topdir = "/topdir";
        char *linkdir = "/linkdir";
        char *subdir = "./subdir";
        char *respath = NULL;
        char pathbuf[4096];

        ret = glfs_mkdir (fs, topdir, 0755);
        if (ret) {
                fprintf (stderr, "mkdir(%s): %s\n", topdir, strerror (errno));
                return -1;
        }

        respath = glfs_getcwd (fs, pathbuf, 4096);
        fprintf (stdout, "getcwd() = %s\n", respath);

        ret = glfs_symlink (fs, topdir, linkdir);
        if (ret) {
                fprintf (stderr, "symlink(%s, %s): %s\n", topdir, linkdir, strerror (errno));
                return -1;
        }

        ret = glfs_chdir (fs, linkdir);
        if (ret) {
                fprintf (stderr, "chdir(%s): %s\n", linkdir, strerror (errno));
                return -1;
        }

        respath = glfs_getcwd (fs, pathbuf, 4096);
        fprintf (stdout, "getcwd() = %s\n", respath);

        respath = glfs_realpath (fs, subdir, pathbuf);
        if (respath) {
                fprintf (stderr, "realpath(%s) worked unexpectedly: %s\n", subdir, respath);
                return -1;
        }

        ret = glfs_mkdir (fs, subdir, 0755);
        if (ret) {
                fprintf (stderr, "mkdir(%s): %s\n", subdir, strerror (errno));
                return -1;
        }

        respath = glfs_realpath (fs, subdir, pathbuf);
        if (!respath) {
                fprintf (stderr, "realpath(%s): %s\n", subdir, strerror (errno));
        } else {
                fprintf (stdout, "realpath(%s) = %s\n", subdir, respath);
        }

        ret = glfs_chdir (fs, subdir);
        if (ret) {
                fprintf (stderr, "chdir(%s): %s\n", subdir, strerror (errno));
                return -1;
        }

        respath = glfs_getcwd (fs, pathbuf, 4096);
        fprintf (stdout, "getcwd() = %s\n", respath);

        respath = glfs_realpath (fs, "/linkdir/subdir", pathbuf);
        if (!respath) {
                fprintf (stderr, "realpath(/linkdir/subdir): %s\n", strerror (errno));
        } else {
                fprintf (stdout, "realpath(/linkdir/subdir) = %s\n", respath);
        }

        return 0;
}

#ifdef DEBUG
static void
peek_stat (struct stat *sb)
{
        printf ("Dumping stat information:\n");
        printf ("File type:                ");

        switch (sb->st_mode & S_IFMT) {
                case S_IFBLK:  printf ("block device\n");            break;
                case S_IFCHR:  printf ("character device\n");        break;
                case S_IFDIR:  printf ("directory\n");               break;
                case S_IFIFO:  printf ("FIFO/pipe\n");               break;
                case S_IFLNK:  printf ("symlink\n");                 break;
                case S_IFREG:  printf ("regular file\n");            break;
                case S_IFSOCK: printf ("socket\n");                  break;
                default:       printf ("unknown?\n");                break;
        }

        printf ("I-node number:            %ld\n", (long) sb->st_ino);

        printf ("Mode:                     %lo (octal)\n",
                (unsigned long) sb->st_mode);

        printf ("Link count:               %ld\n", (long) sb->st_nlink);
        printf ("Ownership:                UID=%ld   GID=%ld\n",
                (long) sb->st_uid, (long) sb->st_gid);

        printf ("Preferred I/O block size: %ld bytes\n",
                (long) sb->st_blksize);
        printf ("File size:                %lld bytes\n",
                (long long) sb->st_size);
        printf ("Blocks allocated:         %lld\n",
                (long long) sb->st_blocks);

        printf ("Last status change:       %s", ctime(&sb->st_ctime));
        printf ("Last file access:         %s", ctime(&sb->st_atime));
        printf ("Last file modification:   %s", ctime(&sb->st_mtime));

        return;
}

static void
peek_handle (unsigned char *glid)
{
        int i;

        for (i = 0; i < GFAPI_HANDLE_LENGTH; i++)
        {
                printf (":%02x:", glid[i]);
        }
        printf ("\n");
}
#else /* DEBUG */
static void
peek_stat (struct stat *sb)
{
        return;
}

static void
peek_handle (unsigned char *id)
{
        return;
}
#endif /* DEBUG */

glfs_t    *fs = NULL;
char      *full_parent_name = "/testdir", *parent_name = "testdir";

void
test_h_unlink (void)
{
        char               *my_dir = "unlinkdir";
        char               *my_file = "file.txt";
        char               *my_subdir = "dir1";
        struct glfs_object *parent = NULL, *leaf = NULL, *dir = NULL,
                           *subdir = NULL, *subleaf = NULL;
        struct stat         sb;
        int                 ret;

        printf ("glfs_h_unlink tests: In Progress\n");

        /* Prepare tests */
        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, NULL, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dir = glfs_h_mkdir (fs, parent, my_dir, 0644, &sb);
        if (dir == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, parent, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        leaf = glfs_h_creat (fs, dir, my_file, O_CREAT, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, dir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        subdir = glfs_h_mkdir (fs, dir, my_subdir, 0644, &sb);
        if (subdir == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_subdir, dir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        subleaf = glfs_h_creat (fs, subdir, my_file, O_CREAT, 0644, &sb);
        if (subleaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, subdir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink non empty directory */
        ret = glfs_h_unlink (fs, dir, my_subdir);
        if ((ret && errno != ENOTEMPTY) || (ret == 0)) {
                fprintf (stderr, "glfs_h_unlink: error unlinking %s: it is non empty: %s\n",
                         my_subdir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink regular file */
        ret = glfs_h_unlink (fs, subdir, my_file);
        if (ret) {
                fprintf (stderr, "glfs_h_unlink: error unlinking %s: from (%p),%s\n",
                         my_file, subdir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink directory */
        ret = glfs_h_unlink (fs, dir, my_subdir);
        if (ret) {
                fprintf (stderr, "glfs_h_unlink: error unlinking %s: from (%p),%s\n",
                         my_subdir, dir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink regular file */
        ret = glfs_h_unlink (fs, dir, my_file);
        if (ret) {
                fprintf (stderr, "glfs_h_unlink: error unlinking %s: from (%p),%s\n",
                         my_file, dir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink non-existent regular file */
        ret = glfs_h_unlink (fs, dir, my_file);
        if ((ret && errno != ENOENT) || (ret == 0)) {
                fprintf (stderr, "glfs_h_unlink: error unlinking non-existent %s: invalid errno ,%d, %s\n",
                         my_file, ret, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink non-existent directory */
        ret = glfs_h_unlink (fs, dir, my_subdir);
        if ((ret && errno != ENOENT) || (ret == 0)) {
                fprintf (stderr, "glfs_h_unlink: error unlinking non-existent %s:  invalid errno ,%d, %s\n",
                         my_subdir, ret, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        /* unlink directory */
        ret = glfs_h_unlink (fs, parent, my_dir);
        if (ret) {
                fprintf (stderr, "glfs_h_unlink: error unlinking %s: from (%p),%s\n",
                         my_dir, dir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }

        printf ("glfs_h_unlink tests: PASSED\n");

out:
        if (dir)
                glfs_h_close (dir);
        if (leaf)
                glfs_h_close (leaf);
        if (subdir)
                glfs_h_close (subdir);
        if (subleaf)
                glfs_h_close (subleaf);
        if (parent)
                glfs_h_close (parent);

        return;
}

void
test_h_getsetattrs (void)
{
        char               *my_dir = "attrdir";
        char               *my_file = "attrfile.txt";
        struct glfs_object *parent = NULL, *leaf = NULL, *dir = NULL;
        struct stat         sb, retsb;
        int                 ret, valid;
        struct timespec     timestamp;

        printf("glfs_h_getattrs and setattrs tests: In Progress\n");

        /* Prepare tests */
        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, NULL, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dir = glfs_h_mkdir (fs, parent, my_dir, 0644, &sb);
        if (dir == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, parent, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_creat (fs, dir, my_file, O_CREAT, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, dir, strerror (errno));
                printf ("glfs_h_unlink tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        ret = glfs_h_getattrs (fs, dir, &retsb);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_getattrs: error %s: from (%p),%s\n",
                         my_dir, dir, strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }
        peek_stat (&retsb);
        /* TODO: Compare stat information */

        retsb.st_mode = 00666;
        retsb.st_uid = 1000;
        retsb.st_gid = 1001;
        ret = clock_gettime (CLOCK_REALTIME, &timestamp);
        if(ret != 0) {
                fprintf (stderr, "clock_gettime: error %s\n", strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }
        retsb.st_atim = timestamp;
        retsb.st_mtim = timestamp;
        valid = GFAPI_SET_ATTR_MODE | GFAPI_SET_ATTR_UID | GFAPI_SET_ATTR_GID |
        GFAPI_SET_ATTR_ATIME | GFAPI_SET_ATTR_MTIME;
        peek_stat (&retsb);

        ret = glfs_h_setattrs (fs, dir, &retsb, valid);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_setattrs: error %s: from (%p),%s\n",
                         my_dir, dir, strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }

        memset(&retsb, 0, sizeof (struct stat));
        ret = glfs_h_stat (fs, dir, &retsb);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_stat: error %s: from (%p),%s\n",
                         my_dir, dir, strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }
        peek_stat (&retsb);

        printf ("glfs_h_getattrs and setattrs tests: PASSED\n");
out:
        if (parent)
                glfs_h_close (parent);
        if (leaf)
                glfs_h_close (leaf);
        if (dir)
                glfs_h_close (dir);

        return;
}

void
test_h_truncate (void)
{
        char               *my_dir = "truncatedir";
        char               *my_file = "file.txt";
        struct glfs_object *root = NULL, *parent = NULL, *leaf = NULL;
        struct stat         sb;
        glfs_fd_t          *fd = NULL;
        char                buf[32];
        off_t               offset = 0;
        int                 ret = 0;

        printf("glfs_h_truncate tests: In Progress\n");

        /* Prepare tests */
        root = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, NULL, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        parent = glfs_h_mkdir (fs, root, my_dir, 0644, &sb);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, root, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_creat (fs, parent, my_file, O_CREAT, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, parent, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        fd = glfs_h_open (fs, leaf, O_RDWR);
        if (fd == NULL) {
                fprintf (stderr, "glfs_h_open: error on open of %s: %s\n",
                         my_file, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }

        memcpy (buf, "abcdefghijklmnopqrstuvwxyz012345", 32);
        ret = glfs_write (fd, buf, 32, 0);

        /* run tests */
        /* truncate lower */
        offset = 30;
        ret = glfs_h_truncate (fs, leaf, offset);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_truncate: error creating %s: from (%p),%s\n",
                         my_file, parent, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        ret = glfs_h_getattrs (fs, leaf, &sb);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_getattrs: error for %s (%p),%s\n",
                         my_file, leaf, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        if (sb.st_size != offset) {
                fprintf (stderr, "glfs_h_truncate: post size mismatch\n");
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }

        /* truncate higher */
        offset = 32;
        ret = glfs_h_truncate (fs, leaf, offset);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_truncate: error creating %s: from (%p),%s\n",
                         my_file, parent, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        ret = glfs_h_getattrs (fs, leaf, &sb);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_getattrs: error for %s (%p),%s\n",
                         my_file, leaf, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        if (sb.st_size != offset) {
                fprintf (stderr, "glfs_h_truncate: post size mismatch\n");
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }

        /* truncate equal */
        offset = 30;
        ret = glfs_h_truncate (fs, leaf, offset);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_truncate: error creating %s: from (%p),%s\n",
                         my_file, parent, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        ret = glfs_h_getattrs (fs, leaf, &sb);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_getattrs: error for %s (%p),%s\n",
                         my_file, leaf, strerror (errno));
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }
        if (sb.st_size != offset) {
                fprintf (stderr, "glfs_h_truncate: post size mismatch\n");
                printf ("glfs_h_truncate tests: FAILED\n");
                goto out;
        }

        printf ("glfs_h_truncate tests: PASSED\n");
out:
        if (fd)
                glfs_close (fd);
        if (root)
                glfs_h_close (root);
        if (parent)
                glfs_h_close (parent);
        if (leaf)
                glfs_h_close (leaf);

        return;
}

void
test_h_links (void)
{
        char               *my_dir = "linkdir";
        char               *my_file = "file.txt";
        char               *my_symlnk = "slnk.txt";
        char               *my_lnk = "lnk.txt";
        char               *linksrc_dir = "dir1";
        char               *linktgt_dir = "dir2";
        struct glfs_object *root = NULL, *parent = NULL, *leaf = NULL,
                           *dirsrc = NULL, *dirtgt = NULL, *dleaf = NULL;
        struct glfs_object *ln1 = NULL;
        struct stat         sb;
        int                 ret;
        char               *buf = NULL;

        printf("glfs_h_link(s) tests: In Progress\n");

        /* Prepare tests */
        root = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, NULL, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        parent = glfs_h_mkdir (fs, root, my_dir, 0644, &sb);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, root, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_creat (fs, parent, my_file, O_CREAT, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, parent, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dirsrc = glfs_h_mkdir (fs, parent, linksrc_dir, 0644, &sb);
        if (dirsrc == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         linksrc_dir, parent, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dirtgt = glfs_h_mkdir (fs, parent, linktgt_dir, 0644, &sb);
        if (dirtgt == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         linktgt_dir, parent, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dleaf = glfs_h_creat (fs, dirsrc, my_file, O_CREAT, 0644, &sb);
        if (dleaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, dirsrc, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* run tests */
        /* sym link: /testdir/linkdir/file.txt to ./slnk.txt */
        ln1 = glfs_h_symlink (fs, parent, my_symlnk, "./file.txt", &sb);
        if (ln1 == NULL) {
                fprintf (stderr, "glfs_h_symlink: error creating %s: from (%p),%s\n",
                         my_symlnk, parent, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        buf = calloc (1024, sizeof(char));
        if (buf == NULL) {
                fprintf (stderr, "Error allocating memory\n");
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }

        ret = glfs_h_readlink (fs, ln1, buf, 1024);
        if (ret <= 0) {
                fprintf (stderr, "glfs_h_readlink: error reading %s: from (%p),%s\n",
                         my_symlnk, ln1, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        if (!(strncmp (buf, my_symlnk, strlen (my_symlnk)))) {
                fprintf (stderr, "glfs_h_readlink: error mismatch in link name: actual %s: retrieved %s\n",
                         my_symlnk, buf);
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }

        /* link: /testdir/linkdir/file.txt to ./lnk.txt */
        ret = glfs_h_link (fs, leaf, parent, my_lnk);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_link: error creating %s: from (%p),%s\n",
                         my_lnk, parent, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        /* TODO: Should write content to a file and read from the link */

        /* link: /testdir/linkdir/dir1/file.txt to ../dir2/slnk.txt */
        ret = glfs_h_link (fs, dleaf, dirtgt, my_lnk);
        if (ret != 0) {
                fprintf (stderr, "glfs_h_link: error creating %s: from (%p),%s\n",
                         my_lnk, dirtgt, strerror (errno));
                printf ("glfs_h_link(s) tests: FAILED\n");
                goto out;
        }
        /* TODO: Should write content to a file and read from the link */

        printf ("glfs_h_link(s) tests: PASSED\n");

out:
        if (root)
                glfs_h_close (root);
        if (parent)
                glfs_h_close (parent);
        if (leaf)
                glfs_h_close (leaf);
        if (dirsrc)
                glfs_h_close (dirsrc);
        if (dirtgt)
                glfs_h_close (dirtgt);
        if (dleaf)
                glfs_h_close (dleaf);
        if (ln1)
                glfs_h_close (ln1);
        if (buf)
                free (buf);

        return;
}

void
test_h_rename (void)
{
        char               *my_dir = "renamedir";
        char               *my_file = "file.txt";
        char               *src_dir = "dir1";
        char               *tgt_dir = "dir2";
        struct glfs_object *root = NULL, *parent = NULL, *leaf = NULL,
                           *dirsrc = NULL, *dirtgt = NULL, *dleaf = NULL;
        struct stat         sb;
        int                 ret;

        printf("glfs_h_rename tests: In Progress\n");

        /* Prepare tests */
        root = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, NULL, strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        parent = glfs_h_mkdir (fs, root, my_dir, 0644, &sb);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, root, strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_creat (fs, parent, my_file, O_CREAT, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, parent, strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dirsrc = glfs_h_mkdir (fs, parent, src_dir, 0644, &sb);
        if (dirsrc == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         src_dir, parent, strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dirtgt = glfs_h_mkdir (fs, parent, tgt_dir, 0644, &sb);
        if (dirtgt == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         tgt_dir, parent, strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        dleaf = glfs_h_creat (fs, dirsrc, my_file, O_CREAT, 0644, &sb);
        if (dleaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                         my_file, dirsrc, strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* run tests */
        /* Rename file.txt -> file1.txt */
        ret = glfs_h_rename (fs, parent, "file.txt", parent, "file1.txt");
        if (ret != 0) {
                fprintf (stderr, "glfs_h_rename: error renaming %s to %s (%s)\n",
                         "file.txt", "file1.txt", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        /* rename dir1/file.txt -> file.txt */
        ret = glfs_h_rename (fs, dirsrc, "file.txt", parent, "file.txt");
        if (ret != 0) {
                fprintf (stderr, "glfs_h_rename: error renaming %s/%s to %s (%s)\n",
                         src_dir, "file.txt", "file.txt", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        /* rename file1.txt -> file.txt (exists) */
        ret = glfs_h_rename (fs, parent, "file1.txt", parent, "file.txt");
        if (ret != 0) {
                fprintf (stderr, "glfs_h_rename: error renaming %s to %s (%s)\n",
                         "file.txt", "file.txt", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        /* rename dir1 -> dir3 */
        ret = glfs_h_rename (fs, parent, "dir1", parent, "dir3");
        if (ret != 0) {
                fprintf (stderr, "glfs_h_rename: error renaming %s to %s (%s)\n",
                         "dir1", "dir3", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        /* rename dir2 ->dir3 (exists) */
        ret = glfs_h_rename (fs, parent, "dir2", parent, "dir3");
        if (ret != 0) {
                fprintf (stderr, "glfs_h_rename: error renaming %s to %s (%s)\n",
                         "dir2", "dir3", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        /* rename file.txt -> dir3 (fail) */
        ret = glfs_h_rename (fs, parent, "file.txt", parent, "dir3");
        if (ret == 0) {
                fprintf (stderr, "glfs_h_rename: NO error renaming %s to %s (%s)\n",
                         "file.txt", "dir3", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        /* rename dir3 -> file.txt (fail) */
        ret = glfs_h_rename (fs, parent, "dir3", parent, "file.txt");
        if (ret == 0) {
                fprintf (stderr, "glfs_h_rename: NO error renaming %s to %s (%s)\n",
                         "dir3", "file.txt", strerror (errno));
                printf ("glfs_h_rename tests: FAILED\n");
                goto out;
        }

        printf ("glfs_h_rename tests: PASSED\n");

out:
        if (root)
                glfs_h_close (root);
        if (parent)
                glfs_h_close (parent);
        if (leaf)
                glfs_h_close (leaf);
        if (dirsrc)
                glfs_h_close (dirsrc);
        if (dirtgt)
                glfs_h_close (dirtgt);
        if (dleaf)
                glfs_h_close (dleaf);

        return;
}

void
assimilatetime (struct timespec *ts, struct timespec ts_st,
                struct timespec ts_ed)
{
        if ((ts_ed.tv_nsec - ts_st.tv_nsec) < 0) {
                ts->tv_sec += ts_ed.tv_sec - ts_st.tv_sec - 1;
                ts->tv_nsec += 1000000000 + ts_ed.tv_nsec - ts_st.tv_nsec;
        } else {
                ts->tv_sec += ts_ed.tv_sec - ts_st.tv_sec;
                ts->tv_nsec += ts_ed.tv_nsec - ts_st.tv_nsec;
        }

        if (ts->tv_nsec > 1000000000) {
                ts->tv_nsec = ts->tv_nsec - 1000000000;
                ts->tv_sec += 1;
        }

        return;
}

#define MAX_FILES_CREATE 10
#define MAXPATHNAME      512
void
test_h_performance (void)
{
        char               *my_dir = "perftest",
                           *full_dir_path="/testdir/perftest";
        char               *my_file = "file_", my_file_name[MAXPATHNAME];
        struct glfs_object *parent = NULL, *leaf = NULL, *dir = NULL;
        struct stat         sb;
        int                 ret, i;
        struct glfs_fd     *fd;
        struct timespec     c_ts = {0, 0}, c_ts_st, c_ts_ed;
        struct timespec     o_ts = {0, 0}, o_ts_st, o_ts_ed;

        printf("glfs_h_performance tests: In Progress\n");

        /* Prepare tests */
        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, NULL, strerror (errno));
                printf ("glfs_h_performance tests: FAILED\n");
                goto out;
        }

        dir = glfs_h_mkdir (fs, parent, my_dir, 0644, &sb);
        if (dir == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, parent, strerror (errno));
                printf ("glfs_h_performance tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* create performance */
        ret = clock_gettime (CLOCK_REALTIME, &o_ts_st);
        if(ret != 0) {
                fprintf (stderr, "clock_gettime: error %s\n", strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }

        for (i = 0; i < MAX_FILES_CREATE; i++) {
                sprintf (my_file_name, "%s%d", my_file, i);

                ret = clock_gettime (CLOCK_REALTIME, &c_ts_st);
                if(ret != 0) {
                        fprintf (stderr, "clock_gettime: error %s\n",
                                 strerror (errno));
                        printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                        goto out;
                }

                leaf = glfs_h_lookupat (fs, dir, my_file_name, &sb, 0);
                if (leaf != NULL) {
                        fprintf (stderr, "glfs_h_lookup: exists %s\n",
                                 my_file_name);
                        printf ("glfs_h_performance tests: FAILED\n");
                        goto out;
                }

                leaf = glfs_h_creat (fs, dir, my_file_name, O_CREAT, 0644, &sb);
                if (leaf == NULL) {
                        fprintf (stderr, "glfs_h_creat: error creating %s: from (%p),%s\n",
                                 my_file, dir, strerror (errno));
                        printf ("glfs_h_performance tests: FAILED\n");
                        goto out;
                }

                ret = clock_gettime (CLOCK_REALTIME, &c_ts_ed);
                if(ret != 0) {
                        fprintf (stderr, "clock_gettime: error %s\n",
                                 strerror (errno));
                        printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                        goto out;
                }

                assimilatetime (&c_ts, c_ts_st, c_ts_ed);
                glfs_h_close (leaf); leaf = NULL;
        }

        ret = clock_gettime (CLOCK_REALTIME, &o_ts_ed);
        if(ret != 0) {
                fprintf (stderr, "clock_gettime: error %s\n", strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }

        assimilatetime (&o_ts, o_ts_st, o_ts_ed);

        printf ("Creation performance (handle based):\n\t# empty files:%d\n",
                MAX_FILES_CREATE);
        printf ("\tOverall time:\n\t\tSecs:%ld\n\t\tnSecs:%ld\n",
                o_ts.tv_sec, o_ts.tv_nsec);
        printf ("\tcreate call time time:\n\t\tSecs:%ld\n\t\tnSecs:%ld\n",
                c_ts.tv_sec, c_ts.tv_nsec);

        /* create using path */
        c_ts.tv_sec = o_ts.tv_sec = 0;
        c_ts.tv_nsec = o_ts.tv_nsec = 0;

        sprintf (my_file_name, "%s1", full_dir_path);
        ret = glfs_mkdir (fs, my_file_name, 0644);
        if (ret != 0) {
                fprintf (stderr, "glfs_mkdir: error creating %s: from (%p),%s\n",
                         my_dir, parent, strerror (errno));
                printf ("glfs_h_performance tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        ret = clock_gettime (CLOCK_REALTIME, &o_ts_st);
        if(ret != 0) {
                fprintf (stderr, "clock_gettime: error %s\n", strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }

        for (i = 0; i < MAX_FILES_CREATE; i++) {
                sprintf (my_file_name, "%s1/%sn%d", full_dir_path, my_file, i);

                ret = clock_gettime (CLOCK_REALTIME, &c_ts_st);
                if(ret != 0) {
                        fprintf (stderr, "clock_gettime: error %s\n",
                                 strerror (errno));
                        printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                        goto out;
                }

                ret = glfs_stat (fs, my_file_name, &sb);
                if (ret == 0) {
                        fprintf (stderr, "glfs_stat: exists %s\n",
                                 my_file_name);
                        printf ("glfs_h_performance tests: FAILED\n");
                        goto out;
                }

                fd = glfs_creat (fs, my_file_name, O_CREAT, 0644);
                if (fd == NULL) {
                        fprintf (stderr, "glfs_creat: error creating %s: from (%p),%s\n",
                                 my_file, dir, strerror (errno));
                        printf ("glfs_h_performance tests: FAILED\n");
                        goto out;
                }

                ret = clock_gettime (CLOCK_REALTIME, &c_ts_ed);
                if(ret != 0) {
                        fprintf (stderr, "clock_gettime: error %s\n",
                                 strerror (errno));
                        printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                        goto out;
                }

                assimilatetime (&c_ts, c_ts_st, c_ts_ed);
                glfs_close (fd);
        }

        ret = clock_gettime (CLOCK_REALTIME, &o_ts_ed);
        if(ret != 0) {
                fprintf (stderr, "clock_gettime: error %s\n", strerror (errno));
                printf ("glfs_h_getattrs and setattrs tests: FAILED\n");
                goto out;
        }

        assimilatetime (&o_ts, o_ts_st, o_ts_ed);

        printf ("Creation performance (path based):\n\t# empty files:%d\n",
                MAX_FILES_CREATE);
        printf ("\tOverall time:\n\t\tSecs:%ld\n\t\tnSecs:%ld\n",
                o_ts.tv_sec, o_ts.tv_nsec);
        printf ("\tcreate call time time:\n\t\tSecs:%ld\n\t\tnSecs:%ld\n",
                c_ts.tv_sec, c_ts.tv_nsec);
out:
        return;
}

int
test_handleops (int argc, char *argv[])
{
        int                 ret = 0;
        glfs_fd_t          *fd = NULL;
        struct stat         sb = {0, };
        struct glfs_object *root = NULL, *parent = NULL, *leaf = NULL,
                           *tmp = NULL;
        char                readbuf[32], writebuf[32];
        unsigned char       leaf_handle[GFAPI_HANDLE_LENGTH];

        char *full_leaf_name = "/testdir/testfile.txt",
             *leaf_name = "testfile.txt",
             *relative_leaf_name = "testdir/testfile.txt";
        char *leaf_name1 = "testfile1.txt";
        char *full_newparent_name = "/testdir/dir1",
             *newparent_name = "dir1";
        char *full_newnod_name = "/testdir/nod1",
             *newnod_name = "nod1";

        /* Initialize test area */
        ret = glfs_mkdir (fs, full_parent_name, 0644);
        if (ret != 0 && errno != EEXIST) {
                fprintf (stderr, "%s: (%p) %s\n", full_parent_name, fd,
                        strerror (errno));
                printf ("Test initialization failed on volume %s\n", argv[1]);
                goto out;
        }
        else if (ret != 0) {
                printf ("Found test directory %s to be existing\n",
                        full_parent_name);
                printf ("Cleanup test directory and restart tests\n");
                goto out;
        }

        fd = glfs_creat (fs, full_leaf_name, O_CREAT, 0644);
        if (fd == NULL) {
                fprintf (stderr, "%s: (%p) %s\n", full_leaf_name, fd,
                        strerror (errno));
                printf ("Test initialization failed on volume %s\n", argv[1]);
                goto out;
        }
        glfs_close (fd);

        printf ("Initialized the test area, within volume %s\n", argv[1]);

        /* Handle based APIs test area */

        /* glfs_lookupat test */
        printf ("glfs_h_lookupat tests: In Progress\n");
        /* start at root of the volume */
        root = glfs_h_lookupat (fs, NULL, "/", &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         "/", NULL, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* lookup a parent within root */
        parent = glfs_h_lookupat (fs, root, parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         parent_name, root, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* lookup a leaf/child within the parent */
        leaf = glfs_h_lookupat (fs, parent, leaf_name, &sb, 0);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         leaf_name, parent, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* reset */
        glfs_h_close (root); root = NULL;
        glfs_h_close (leaf); leaf = NULL;
        glfs_h_close (parent); parent = NULL;

        /* check absolute paths */
        root = glfs_h_lookupat (fs, NULL, "/", &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         "/", NULL, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, root, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_lookupat (fs, NULL, full_leaf_name, &sb, 0);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_leaf_name, parent, strerror (errno));
                printf ("glfs_h_lookupat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* reset */
        glfs_h_close (leaf); leaf = NULL;

        /* check multiple component paths */
        leaf = glfs_h_lookupat (fs, root, relative_leaf_name, &sb, 0);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         relative_leaf_name, parent, strerror (errno));
                goto out;
        }
        peek_stat (&sb);

        /* reset */
        glfs_h_close (root); root = NULL;
        glfs_h_close (parent); parent = NULL;

        /* check symlinks in path */

        /* TODO: -ve test cases */
        /* parent invalid
        * path invalid
        * path does not exist after some components
        * no parent, but relative path
        * parent and full path? -ve?
        */

        printf ("glfs_h_lookupat tests: PASSED\n");

        /* glfs_openat test */
        printf ("glfs_h_open tests: In Progress\n");
        fd = glfs_h_open (fs, leaf, O_RDWR);
        if (fd == NULL) {
                fprintf (stderr, "glfs_h_open: error on open of %s: %s\n",
                         full_leaf_name, strerror (errno));
                printf ("glfs_h_open tests: FAILED\n");
                goto out;
        }

        /* test read/write based on fd */
        memcpy (writebuf, "abcdefghijklmnopqrstuvwxyz012345", 32);
        ret = glfs_write (fd, writebuf, 32, 0);

        glfs_lseek (fd, 0, SEEK_SET);

        ret = glfs_read (fd, readbuf, 32, 0);
        if (memcmp (readbuf, writebuf, 32)) {
                printf ("Failed to read what I wrote: %s %s\n", readbuf,
                        writebuf);
                glfs_close (fd);
                printf ("glfs_h_open tests: FAILED\n");
                goto out;
        }

        glfs_h_close (leaf); leaf = NULL;
        glfs_close (fd);

        printf ("glfs_h_open tests: PASSED\n");

        /* Create tests */
        printf ("glfs_h_creat tests: In Progress\n");
        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, root, strerror (errno));
                printf ("glfs_h_creat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_creat (fs, parent, leaf_name1, O_CREAT, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_creat: error on create of %s: from (%p),%s\n",
                         leaf_name1, parent, strerror (errno));
                printf ("glfs_h_creat tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        glfs_h_close (leaf); leaf = NULL;

        leaf = glfs_h_creat (fs, parent, leaf_name1, O_CREAT | O_EXCL, 0644,
                            &sb);
        if (leaf != NULL || errno != EEXIST) {
                fprintf (stderr, "glfs_h_creat: existing file, leaf = (%p), errno = %s\n",
                        leaf, strerror (errno));
                printf ("glfs_h_creat tests: FAILED\n");
                if (leaf != NULL) {
                        glfs_h_close (leaf); leaf = NULL;
                }
        }

        tmp = glfs_h_creat (fs, root, parent_name, O_CREAT, 0644, &sb);
        if (tmp != NULL || !(errno == EISDIR || errno == EINVAL)) {
                fprintf (stderr, "glfs_h_creat: dir create, tmp = (%p), errno = %s\n",
                        leaf, strerror (errno));
                printf ("glfs_h_creat tests: FAILED\n");
                if (tmp != NULL) {
                        glfs_h_close (tmp); tmp = NULL;
                }
        }

        /* TODO: Other combinations and -ve cases as applicable */
        printf ("glfs_h_creat tests: PASSED\n");

        /* extract handle and create from handle test */
        printf ("glfs_h_extract_handle and glfs_h_create_from_handle tests: In Progress\n");
        /* TODO: Change the lookup to creat below for a GIFD recovery falure,
         * that needs to be fixed */
        leaf = glfs_h_lookupat (fs, parent, leaf_name1, &sb, 0);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         leaf_name1, parent, strerror (errno));
                printf ("glfs_h_extract_handle tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        ret = glfs_h_extract_handle (leaf, leaf_handle,
                                             GFAPI_HANDLE_LENGTH);
        if (ret < 0) {
                fprintf (stderr, "glfs_h_extract_handle: error extracting handle of %s: %s\n",
                         full_leaf_name, strerror (errno));
                printf ("glfs_h_extract_handle tests: FAILED\n");
                goto out;
        }
        peek_handle (leaf_handle);

        glfs_h_close (leaf); leaf = NULL;

        leaf = glfs_h_create_from_handle (fs, leaf_handle, GFAPI_HANDLE_LENGTH,
                                          &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_create_from_handle: error on create of %s: from (%p),%s\n",
                         leaf_name1, leaf_handle, strerror (errno));
                printf ("glfs_h_create_from_handle tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        fd = glfs_h_open (fs, leaf, O_RDWR);
        if (fd == NULL) {
                fprintf (stderr, "glfs_h_open: error on open of %s: %s\n",
                         full_leaf_name, strerror (errno));
                printf ("glfs_h_create_from_handle tests: FAILED\n");
                goto out;
        }

        /* test read/write based on fd */
        memcpy (writebuf, "abcdefghijklmnopqrstuvwxyz012345", 32);
        ret = glfs_write (fd, writebuf, 32, 0);

        glfs_lseek (fd, 0, SEEK_SET);

        ret = glfs_read (fd, readbuf, 32, 0);
        if (memcmp (readbuf, writebuf, 32)) {
                printf ("Failed to read what I wrote: %s %s\n", writebuf,
                        writebuf);
                printf ("glfs_h_create_from_handle tests: FAILED\n");
                glfs_close (fd);
                goto out;
        }

        glfs_close (fd);
        glfs_h_close (leaf); leaf = NULL;
        glfs_h_close (parent); parent = NULL;

        printf ("glfs_h_extract_handle and glfs_h_create_from_handle tests: PASSED\n");

        /* Mkdir tests */
        printf ("glfs_h_mkdir tests: In Progress\n");

        ret = glfs_rmdir (fs, full_newparent_name);
        if (ret && errno != ENOENT) {
                fprintf (stderr, "glfs_rmdir: Failed for %s: %s\n",
                         full_newparent_name, strerror (errno));
                printf ("glfs_h_mkdir tests: FAILED\n");
                goto out;
        }

        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, root, strerror (errno));
                printf ("glfs_h_mkdir tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_mkdir (fs, parent, newparent_name, 0644, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error on mkdir of %s: from (%p),%s\n",
                         newparent_name, parent, strerror (errno));
                printf ("glfs_h_mkdir tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        glfs_h_close (leaf); leaf = NULL;

        leaf = glfs_h_mkdir (fs, parent, newparent_name, 0644, &sb);
        if (leaf != NULL || errno != EEXIST) {
                fprintf (stderr, "glfs_h_mkdir: existing directory, leaf = (%p), errno = %s\n",
                         leaf, strerror (errno));
                printf ("glfs_h_mkdir tests: FAILED\n");
                if (leaf != NULL) {
                        glfs_h_close (leaf); leaf = NULL;
                }
        }

        glfs_h_close (parent); parent = NULL;

        printf ("glfs_h_mkdir tests: PASSED\n");

        /* Mknod tests */
        printf ("glfs_h_mknod tests: In Progress\n");
        ret = glfs_unlink (fs, full_newnod_name);
        if (ret && errno != ENOENT) {
                fprintf (stderr, "glfs_unlink: Failed for %s: %s\n",
                         full_newnod_name, strerror (errno));
                printf ("glfs_h_mknod tests: FAILED\n");
                goto out;
        }

        parent = glfs_h_lookupat (fs, NULL, full_parent_name, &sb, 0);
        if (parent == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of %s: from (%p),%s\n",
                         full_parent_name, root, strerror (errno));
                printf ("glfs_h_mknod tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        leaf = glfs_h_mknod (fs, parent, newnod_name, S_IFIFO, 0, &sb);
        if (leaf == NULL) {
                fprintf (stderr, "glfs_h_mkdir: error on mkdir of %s: from (%p),%s\n",
                         newnod_name, parent, strerror (errno));
                printf ("glfs_h_mknod tests: FAILED\n");
                goto out;
        }
        peek_stat (&sb);

        /* TODO: creat op on a FIFO node hangs, need to check and fix
        tmp = glfs_h_creat (fs, parent, newnod_name, O_CREAT, 0644, &sb);
        if (tmp != NULL || errno != EINVAL) {
                fprintf (stderr, "glfs_h_creat: node create, tmp = (%p), errno = %s\n",
                        tmp, strerror (errno));
                printf ("glfs_h_creat/mknod tests: FAILED\n");
                if (tmp != NULL) {
                        glfs_h_close(tmp); tmp = NULL;
                }
        } */

        glfs_h_close (leaf); leaf = NULL;

        leaf = glfs_h_mknod (fs, parent, newnod_name, 0644, 0, &sb);
        if (leaf != NULL || errno != EEXIST) {
                fprintf (stderr, "glfs_h_mknod: existing node, leaf = (%p), errno = %s\n",
                         leaf, strerror (errno));
                printf ("glfs_h_mknod tests: FAILED\n");
                if (leaf != NULL) {
                        glfs_h_close (leaf); leaf = NULL;
                }
        }

        glfs_h_close (parent); parent = NULL;

        printf ("glfs_h_mknod tests: PASSED\n");

        /* unlink tests */
        test_h_unlink ();

        /* TODO: opendir tests */

        /* getattr tests */
        test_h_getsetattrs ();

        /* TODO: setattr tests */

        /* truncate tests */
        test_h_truncate();

        /* link tests */
        test_h_links ();

        /* rename tests */
        test_h_rename ();

        /* performance tests */
        test_h_performance ();

        /* END: New APIs test area */

out:
        /* Cleanup glfs handles */
        if (root)
                glfs_h_close (root);
        if (parent)
                glfs_h_close (parent);
        if (leaf)
                glfs_h_close (leaf);

        return ret;
}

int
main (int argc, char *argv[])
{
        glfs_t    *fs2 = NULL;
        int        ret = 0;
        glfs_fd_t *fd = NULL;
        glfs_fd_t *fd2 = NULL;
        struct stat sb = {0, };
        char       readbuf[32];
        char       writebuf[32];

        char      *filename = "/filename2";

        if (argc != 3) {
                printf ("Expect following args\n\t%s <volname> <hostname>\n", argv[0]);
                return -1;
        }

        fs = glfs_new (argv[1]);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }

//      ret = glfs_set_volfile (fs, "/tmp/posix.vol");

        ret = glfs_set_volfile_server (fs, "tcp", argv[2], 24007);

//      ret = glfs_set_volfile_server (fs, "unix", "/tmp/gluster.sock", 0);

        ret = glfs_set_logging (fs, "/dev/stderr", 7);

        ret = glfs_init (fs);

        fprintf (stderr, "glfs_init: returned %d\n", ret);

        sleep (2);

        fs2 = glfs_new (argv[1]);
        if (!fs2) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }


//      ret = glfs_set_volfile (fs2, "/tmp/posix.vol");

        ret = glfs_set_volfile_server (fs2, "tcp", argv[2], 24007);

        ret = glfs_set_logging (fs2, "/dev/stderr", 7);

        ret = glfs_init (fs2);

        fprintf (stderr, "glfs_init: returned %d\n", ret);

        ret = glfs_lstat (fs, filename, &sb);
        fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

        fd = glfs_creat (fs, filename, O_RDWR, 0644);
        fprintf (stderr, "%s: (%p) %s\n", filename, fd, strerror (errno));

        fd2 = glfs_open (fs2, filename, O_RDWR);
        fprintf (stderr, "%s: (%p) %s\n", filename, fd, strerror (errno));

        sprintf (writebuf, "hi there\n");
        ret = glfs_write (fd, writebuf, 32, 0);

        glfs_lseek (fd2, 0, SEEK_SET);

        ret = glfs_read (fd2, readbuf, 32, 0);

        printf ("read %d, %s", ret, readbuf);

        glfs_close (fd);
        glfs_close (fd2);

        filename = "/filename3";
        ret = glfs_mknod (fs, filename, S_IFIFO, 0);
        fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

        ret = glfs_lstat (fs, filename, &sb);
        fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));


        ret = glfs_rename (fs, filename, "/filename4");
        fprintf (stderr, "rename(%s): (%d) %s\n", filename, ret,
                 strerror (errno));

        ret = glfs_unlink (fs, "/filename4");
        fprintf (stderr, "unlink(%s): (%d) %s\n", "/filename4", ret,
                 strerror (errno));

        filename = "/dirname2";
        ret = glfs_mkdir (fs, filename, 0);
        fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

        ret = glfs_lstat (fs, filename, &sb);
        fprintf (stderr, "lstat(%s): (%d) %s\n", filename, ret, strerror (errno));

        ret = glfs_rmdir (fs, filename);
        fprintf (stderr, "rmdir(%s): (%d) %s\n", filename, ret, strerror (errno));

        test_dirops (fs);

        test_xattr (fs);

        test_chdir (fs);

        test_handleops (argc, argv);
        // done

        glfs_fini (fs);
        glfs_fini (fs2);

        return ret;
}
