#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_SIZE (128*1024)
#define READ_WRITE_LOOP 100
#define FOP_LOOP_COUNT 20
#define TEST_CASE_LOOP 20

int gfapi = 1;
static int extension = 1;

static int
large_number_of_fops (glfs_t *fs) {
        int       ret       = 0;
        int       i         = 0;
        glfs_fd_t *fd       = NULL;
        glfs_fd_t *fd1      = NULL;
        char      *dir1     = NULL, *dir2 = NULL, *filename1 = NULL, *filename2 = NULL;
        char      *buf      = NULL;
        struct stat sb      = {0, };

        for (i = 0 ; i < FOP_LOOP_COUNT ; i++) {
                ret = asprintf (&dir1, "dir%d", extension);
                if (ret < 0) {
                        fprintf (stderr, "cannot construct filename (%s)",
                                 strerror (errno));
                        return ret;
                }

                extension++;

                ret = glfs_mkdir (fs, dir1, 0755);
                if (ret < 0) {
                        fprintf (stderr, "mkdir(%s): %s\n", dir1, strerror (errno));
                        return -1;
                }

                fd = glfs_opendir (fs, dir1);
                if (!fd) {
                        fprintf (stderr, "/: %s\n", strerror (errno));
                        return -1;
                }

                ret = glfs_fsetxattr (fd, "user.dirfattr", "fsetxattr", 8, 0);
                if (ret < 0) {
                        fprintf (stderr, "fsetxattr(%s): %d (%s)\n", dir1, ret,
                                strerror (errno));
                        return -1;
                }

                ret = glfs_closedir (fd);
                if (ret < 0) {
                        fprintf (stderr, "glfs_closedir failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = glfs_rmdir (fs, dir1);
                if (ret < 0) {
                        fprintf (stderr, "glfs_unlink failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = asprintf (&filename1, "file%d", extension);
                if (ret < 0) {
                        fprintf (stderr, "cannot construct filename (%s)",
                                strerror (errno));
                        return ret;
                }

                ret = asprintf (&filename2, "file-%d", extension);
                if (ret < 0) {
                        fprintf (stderr, "cannot construct filename (%s)",
                                strerror (errno));
                        return ret;
                }

                extension++;

                fd = glfs_creat (fs, filename1, O_RDWR, 0644);
                if (!fd) {
                        fprintf (stderr, "%s: (%p) %s\n", filename1, fd,
                                strerror (errno));
                        return -1;
                }

                ret = glfs_rename (fs, filename1, filename2);
                if (ret < 0) {
                        fprintf (stderr, "glfs_rename failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = glfs_lstat (fs, filename2, &sb);
                if (ret < 0) {
                        fprintf (stderr, "glfs_lstat failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = glfs_close (fd);
                if (ret < 0) {
                        fprintf (stderr, "glfs_close failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = glfs_unlink (fs, filename2);
                if (ret < 0) {
                        fprintf (stderr, "glfs_unlink failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }
        }
}

static int
large_read_write (glfs_t *fs) {

        int       ret       = 0;
        int       j         = 0;
        glfs_fd_t *fd       = NULL;
        glfs_fd_t *fd1      = NULL;
        char      *filename = NULL;
        char      *buf      = NULL;

        ret = asprintf (&filename, "filerw%d", extension);
        if (ret < 0) {
                fprintf (stderr, "cannot construct filename (%s)",
                         strerror (errno));
                return ret;
        }

        extension++;

        fd = glfs_creat (fs, filename, O_RDWR, 0644);
        if (!fd) {
                fprintf (stderr, "%s: (%p) %s\n", filename, fd,
                        strerror (errno));
                return -1;
        }

        buf = (char *) malloc (WRITE_SIZE);
        memset (buf, '-', WRITE_SIZE);

        for (j = 0; j < READ_WRITE_LOOP; j++) {
                ret = glfs_write (fd, buf, WRITE_SIZE, 0);
                if (ret < 0) {
                        fprintf (stderr, "Write(%s): %d (%s)\n", filename, ret,
                                strerror (errno));
                        return ret;
                }
        }

        fd1 = glfs_open (fs, filename, O_RDWR);
        if (fd1 < 0) {
                fprintf (stderr, "Open(%s): %d (%s)\n", filename, ret,
                        strerror (errno));
                return -1;
        }

        glfs_lseek (fd1, 0, SEEK_SET);
        for (j = 0; j < READ_WRITE_LOOP; j++) {
                ret = glfs_read (fd1, buf, WRITE_SIZE, 0);
                if (ret < 0) {
                        fprintf (stderr, "Read(%s): %d (%s)\n", filename, ret,
                                strerror (errno));
                        return ret;
                }
        }

        for (j = 0; j < READ_WRITE_LOOP; j++) {
                ret = glfs_write (fd1, buf, WRITE_SIZE, 0);
                if (ret < 0) {
                        fprintf (stderr, "Write(%s): %d (%s)\n", filename, ret,
                                strerror (errno));
                        return ret;
                }
        }

        glfs_close (fd);
        glfs_close (fd1);
        ret = glfs_unlink (fs, filename);
        if (ret < 0) {
                fprintf (stderr, "glfs_unlink failed with ret: %d (%s)\n",
                        ret, strerror (errno));
                return -1;
        }

        free (buf);
        free (filename);
}

static int
volfile_change (const char *volname) {
        int   ret = 0;
        char *cmd = NULL, *cmd1 = NULL;

        ret = asprintf (&cmd, "gluster volume set %s stat-prefetch off",
                        volname);
        if (ret < 0) {
                fprintf (stderr, "cannot construct cli command string (%s)",
                         strerror (errno));
                return ret;
        }

        ret = asprintf (&cmd1, "gluster volume set %s stat-prefetch on",
                        volname);
        if (ret < 0) {
                fprintf (stderr, "cannot construct cli command string (%s)",
                         strerror (errno));
                return ret;
        }

        ret = system (cmd);
        if (ret < 0) {
                fprintf (stderr, "stat-prefetch off on (%s) failed", volname);
                return ret;
        }

        ret = system (cmd1);
        if (ret < 0) {
                fprintf (stderr, "stat-prefetch on on (%s) failed", volname);
                return ret;
        }

        free (cmd);
        free (cmd1);
        return ret;
}

int
main (int argc, char *argv[])
{
        glfs_t    *fs       = NULL;
        int       ret       = 0;
        int       i         = 0;
        glfs_fd_t *fd       = NULL;
        glfs_fd_t *fd1      = NULL;
        char      *topdir   = "topdir", *filename = "file1";
        char      *buf      = NULL;
        char      *logfile  = NULL;
        char      *hostname = NULL;

        if (argc != 4) {
                fprintf (stderr,
                        "Expect following args %s <hostname> <Vol> <log file>\n"
                        , argv[0]);
                return -1;
        }

        hostname = argv[1];
        logfile = argv[3];

        for (i = 0; i < TEST_CASE_LOOP; i++) {
                fs = glfs_new (argv[2]);
                if (!fs) {
                        fprintf (stderr, "glfs_new: returned NULL (%s)\n",
                                strerror (errno));
                        return -1;
                }

                ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
                if (ret < 0) {
                        fprintf (stderr, "glfs_set_volfile_server failed ret:%d (%s)\n",
                        ret, strerror (errno));
                        return -1;
                }

                ret = glfs_set_logging (fs, logfile, 7);
                if (ret < 0) {
                        fprintf (stderr, "glfs_set_logging failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = glfs_init (fs);
                if (ret < 0) {
                        fprintf (stderr, "glfs_init failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }

                ret = large_number_of_fops (fs);
                if (ret < 0)
                        return -1;

                ret = large_read_write (fs);
                if (ret < 0)
                        return -1;

                ret = volfile_change (argv[2]);
                if (ret < 0)
                        return -1;

                ret = large_number_of_fops (fs);
                if (ret < 0)
                        return -1;

                ret = large_read_write (fs);
                if (ret < 0)
                        return -1;

                ret = glfs_fini (fs);
                if (ret < 0) {
                        fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                                ret, strerror (errno));
                        return -1;
                }
        }
        return 0;
}
