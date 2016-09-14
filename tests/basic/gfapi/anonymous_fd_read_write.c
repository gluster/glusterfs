#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define LOG_ERR(func, ret) do { \
        if (ret != 0) {            \
                fprintf (stderr, "%s : returned error %d (%s)\n", \
                         func, ret, strerror (errno)); \
                goto out; \
        } else { \
                fprintf (stderr, "%s : returned %d\n", func, ret); \
        } \
        } while (0)

int
main (int argc, char *argv[])
{
        int                     ret            = 0;
        glfs_t                  *fs            = NULL;
        struct glfs_object      *root = NULL, *file_obj = NULL;
        struct stat             sb             = {0, };
        char                    readbuf[32], writebuf[32];
        char                    *filename      = "file.txt";
        char                    *logfile       = NULL;
        char                    *volname       = NULL;
        char                    *hostname      = NULL;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                ret = -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("glfs_init", ret);

        root = glfs_h_lookupat (fs, NULL, "/", &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of / ,%s\n",
                         strerror (errno));
                goto out;
        }

        file_obj = glfs_h_creat (fs, root, filename, O_CREAT, 0644, &sb);
        if (file_obj == NULL) {
                fprintf (stderr, "glfs_h_creat: error on create of %s: from (%p),%s\n",
                         filename, root, strerror (errno));
                goto out;
        }

        /* test read/write based on anonymous fd */
        memcpy (writebuf, "abcdefghijklmnopqrstuvwxyz012345", 32);

        ret = glfs_h_anonymous_write (fs, file_obj, writebuf, 32, 0);
        if (ret < 0)
                LOG_ERR ("glfs_h_anonymous_write", ret);

        ret = glfs_h_anonymous_read (fs, file_obj, readbuf, 32, 0);
        if (ret < 0)
                LOG_ERR ("glfs_h_anonymous_read", ret);

        if (memcmp (readbuf, writebuf, 32)) {
                fprintf (stderr, "Failed to read what I wrote: %s %s\n", readbuf,
                         writebuf);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        if (file_obj)
                glfs_h_close (file_obj);

        if (fs) {
                ret = glfs_fini(fs);
                fprintf (stderr, "glfs_fini(fs) returned %d \n", ret);
        }
        if (ret)
                exit(1);
        exit(0);
}
