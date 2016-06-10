#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <alloca.h>
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

#define GLAPI_UUID_LENGTH 16
int
main (int argc, char *argv[])
{
        glfs_t    *fs = NULL;
        glfs_t    *fs2 = NULL;
        int        ret = 0, i;
        glfs_fd_t *fd = NULL;
        char      *filename = "/a1";
        char      *filename2 = "/a2";
        struct     stat sb = {0, };
        struct    glfs_callback_arg cbk;
        char      *logfile = NULL;
        char      *volname = NULL;
        int        cnt = 1;
        int       upcall_received = 0;
        struct glfs_callback_inode_arg *in_arg = NULL;
        struct glfs_object *root = NULL, *leaf = NULL;
        unsigned char   globjhdl[GFAPI_HANDLE_LENGTH];
        unsigned char   globjhdl2[GFAPI_HANDLE_LENGTH];

        cbk.reason = 0;

        fprintf (stderr, "Starting libgfapi_fini\n");
        if (argc != 3) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        volname = argv[1];
        logfile = argv[2];


        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", "localhost", 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("glfs_init", ret);

        fs2 = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }

        ret = glfs_set_volfile_server (fs2, "tcp", "localhost", 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs2, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs2);
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

        root = glfs_h_lookupat (fs2, NULL, "/", &sb, 0);
        if (!root) {
                ret = -1;
                LOG_ERR ("glfs_h_lookupat root", ret);
        }
        leaf = glfs_h_creat (fs2, root, filename, O_RDWR, 0644, &sb);
        if (!leaf) {
                ret = -1;
                LOG_ERR ("glfs_h_lookupat leaf", ret);
        }
        fprintf (stderr, "glfs_h_create leaf - %p\n", leaf);

        while (cnt++ < 5) {
                ret = glfs_h_poll_upcall(fs, &cbk);
                LOG_ERR ("glfs_h_poll_upcall", ret);

                if (cbk.reason == GFAPI_INODE_INVALIDATE) {
                        fprintf (stderr, "Upcall received(%d)\n",
                                 cbk.reason);
                        in_arg = (struct glfs_callback_inode_arg *)(cbk.event_arg);

                        ret = glfs_h_extract_handle (root,
                                                     globjhdl+GLAPI_UUID_LENGTH,
                                                     GFAPI_HANDLE_LENGTH);
                        LOG_ERR("glfs_h_extract_handle", (ret != 16));

                        ret = glfs_h_extract_handle (in_arg->object,
                                                  globjhdl2+GLAPI_UUID_LENGTH,
                                                  GFAPI_HANDLE_LENGTH);
                        LOG_ERR("glfs_h_extract_handle", (ret != 16));

                        if (memcmp (globjhdl+GLAPI_UUID_LENGTH,
                                    globjhdl2+GLAPI_UUID_LENGTH, 16)) {
                                fprintf (stderr, "Error: gfid mismatch\n");
                                exit (1);
                        }
                        upcall_received = 1;
                }
        }

        if (!upcall_received) {
                fprintf (stderr, "Error: Upcall not received\n");
                exit (1);
        }

        ret = glfs_fini(fs);
        LOG_ERR("glfs_fini", ret);

        fprintf (stderr, "End of libgfapi_fini\n");

        exit(0);
}


