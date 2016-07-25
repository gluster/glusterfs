
#include <stdio.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

int
main (int argc, char *argv[])
{
        glfs_t    *fs = NULL;
        glfs_fd_t *fd = NULL;
        int        ret = 1;

        if (argc != 4) {
                fprintf (stderr, "Syntax: %s <host> <volname> <file>\n", argv[0]);
                return 1;
        }

        fs = glfs_new (argv[2]);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", argv[1], 24007);
        if (ret != 0) {
                fprintf (stderr, "glfs_set_volfile_server: retuned %d\n", ret);
                goto out;
        }
        ret = glfs_set_logging (fs, "/dev/null", 7);
        if (ret != 0) {
                fprintf (stderr, "glfs_set_logging: returned %d\n", ret);
                goto out;
        }
        ret = glfs_init (fs);
        if (ret != 0) {
                fprintf (stderr, "glfs_init: returned %d\n", ret);
                goto out;
        }

        fd = glfs_open (fs, argv[3], O_RDWR | O_TRUNC);
        if (fd == NULL) {
                fprintf (stderr, "glfs_open: returned NULL\n");
                goto out;
        }
        glfs_close(fd);

        ret = 0;

out:
        glfs_fini (fs);

        return ret;
}
