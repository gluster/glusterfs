#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
int gfapi = 1;

#define LOG_ERR(func, ret) do { \
        if (ret != 0) {            \
                fprintf (stderr, "%s : returned error ret(%d), errno(%d)\n", \
                         func, ret, errno); \
                exit(1); \
        } else { \
                fprintf (stderr, "%s : returned %d\n", func, ret); \
        } \
        } while (0)
#define LOG_IF_NO_ERR(func, ret) do { \
        if (ret == 0) {            \
                fprintf (stderr, "%s : hasn't returned error %d\n", \
                         func, ret); \
                exit(1); \
        } else { \
                fprintf (stderr, "%s : returned %d\n", func, ret); \
        } \
        } while (0)
int
main (int argc, char *argv[])
{
        glfs_t                   *fs = NULL;
        int                       ret = 0, i;
        glfs_fd_t                *fd = NULL;
        char                     *filename = "/a1";
        char                     *filename2 = "/a2";
        struct stat               sb = {0, };
        struct glfs_upcall       *cbk = NULL;
        char                     *logfile = NULL;
        char                     *volname = NULL;
        int                       cnt = 1;
        struct glfs_upcall_inode *in_arg = NULL;
        struct glfs_object       *root = NULL, *leaf = NULL;

        fprintf (stderr, "Starting libgfapi_fini\n");
        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1]
        volname = argv[2];
        logfile = argv[3];


        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("glfs_init", ret);

        sleep (2);
        root = glfs_h_lookupat (fs, NULL, "/", &sb, 0);
        if (!root) {
                ret = -1;
                LOG_ERR ("glfs_h_lookupat root", ret);
        }
        leaf = glfs_h_lookupat (fs, root, filename, &sb, 0);
        if (!leaf) {
                ret = -1;
                LOG_IF_NO_ERR ("glfs_h_lookupat leaf", ret);
        }

        leaf = glfs_h_creat (fs, root, filename, O_RDWR, 0644, &sb);
        if (!leaf) {
                ret = -1;
                LOG_ERR ("glfs_h_lookupat leaf", ret);
        }
        fprintf (stderr, "glfs_h_create leaf - %p\n", leaf);

        leaf = glfs_h_lookupat (fs, root, filename2, &sb, 0);
        if (!leaf) {
                ret = -1;
                LOG_IF_NO_ERR ("glfs_h_lookupat leaf", ret);
        }

        ret = glfs_h_rename (fs, root, filename, root, filename2);
        LOG_ERR("glfs_rename", ret);

        while (cnt++ < 5) {
                ret = glfs_h_poll_upcall(fs, &cbk);
                LOG_ERR ("glfs_h_poll_upcall", ret);

                /* There should not be any upcalls sent */
                if (glfs_upcall_get_reason(cbk) != GLFS_UPCALL_EVENT_NULL) {
                        fprintf (stderr, "Error: Upcall received(%d)\n",
                                 glfs_upcall_get_reason(cbk));
                        exit (1);
                }

                glfs_free (cbk);
        }

        ret = glfs_fini(fs);
        LOG_ERR("glfs_fini", ret);

        fprintf (stderr, "End of libgfapi_fini\n");

        exit(0);
}


