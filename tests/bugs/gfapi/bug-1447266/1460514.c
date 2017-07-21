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
        struct glfs_object      *root = NULL, *dir = NULL, *subdir = NULL;
        struct stat             sb             = {0, };
        char                    *dirname       = "dir";
        char                    *subdirname    = "subdir";
        char                    *logfile       = NULL;
        char                    *volname       = NULL;
        char                    *hostname      = NULL;
        unsigned char subdir_handle[GFAPI_HANDLE_LENGTH] = {'\0'};

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
                goto out;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("first attempt glfs_init", ret);

        root = glfs_h_lookupat (fs, NULL, "/", &sb, 0);
        if (root == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on lookup of / ,%s\n",
                         strerror (errno));
                goto out;
        }
        dir = glfs_h_mkdir (fs, root, dirname, 0644, &sb);
        if (dir == NULL) {
                 fprintf (stderr, "glfs_h_mkdir: error on directory creation dir ,%s\n",
                         strerror (errno));
                goto out;
        }
        subdir = glfs_h_mkdir (fs, root, subdirname, 0644, &sb);
        if (subdir == NULL) {
                 fprintf (stderr, "glfs_h_mkdir: error on directory creation subdir ,%s\n",
                         strerror (errno));
                goto out;
        }
        ret = glfs_h_extract_handle (subdir, subdir_handle,
                                             GFAPI_HANDLE_LENGTH);
        if (ret < 0) {
                fprintf (stderr, "glfs_h_extract_handle: error extracting handle of %s: %s\n",
                         subdirname, strerror (errno));
                goto out;
        }

        glfs_h_close (subdir);
        subdir = NULL;
        glfs_h_close (dir);
        dir = NULL;

        if (fs) {
                ret = glfs_fini(fs);
                fprintf (stderr, "glfs_fini(fs) returned %d \n", ret);
        }

        fs = NULL;

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                ret = -1;
                goto out;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("second attempt glfs_init", ret);

        subdir = glfs_h_create_from_handle (fs, subdir_handle, GFAPI_HANDLE_LENGTH,
                                          &sb);
        if (subdir == NULL) {
                fprintf (stderr, "glfs_h_create_from_handle: error on create of %s: from (%p),%s\n",
                         subdirname, subdir_handle, strerror (errno));
                goto out;
        }
        dir = glfs_h_lookupat (fs, subdir, "..", &sb, 0);
        if (dir == NULL) {
                fprintf (stderr, "glfs_h_lookupat: error on directory lookup dir using .. ,%s\n",
                         strerror (errno));
                goto out;
        }

out:
        if (subdir)
                glfs_h_close (subdir);
        if (dir)
                glfs_h_close (dir);

        if (fs) {
                ret = glfs_fini(fs);
                fprintf (stderr, "glfs_fini(fs) returned %d \n", ret);
        }

        if (ret)
                exit(1);
        exit(0);
}
