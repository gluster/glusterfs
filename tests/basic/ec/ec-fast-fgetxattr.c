#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

int cbk_complete = 0;
ssize_t cbk_ret_val = 0;
int
fill_iov (struct iovec *iov, char fillchar, int count)
{
        int ret = -1;

        iov->iov_base = calloc (count + 1, sizeof(fillchar));
        if (iov->iov_base == NULL) {
                return ret;
        } else {
                iov->iov_len = count;
                ret = 0;
        }
        memset (iov->iov_base, fillchar, count);
        memset (iov->iov_base + count, '\0', 1);

        return ret;
}

void
write_async_cbk (glfs_fd_t *fd, ssize_t ret, void *cookie)
{

        if (ret < 0) {
                fprintf (stderr, "glfs_write failed");
        }
        cbk_ret_val = ret;
        cbk_complete = 1;
}

int
write_async (glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
        ssize_t         ret          = -1;
        int             flags        = O_RDWR;
        struct iovec    iov          = {0};



        ret = fill_iov (&iov, 'a', char_count);
        if (ret) {
                fprintf (stderr, "failed to create iov");
                goto out;
        }

        ret = glfs_pwritev_async (glfd, &iov, 1, 0, flags, write_async_cbk,
                                  NULL);
out:
        if (ret < 0) {
                fprintf (stderr, "glfs_pwritev async failed");
        }
        return ret;

}

int
main (int argc, char *argv[])
{
        glfs_t    *fs = NULL;
        glfs_fd_t *fd = NULL;
        int        ret = 1;
        char       buf[1024] = {0};

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
        ret = glfs_set_logging (fs, "/tmp/ec-fgetxattr.log", 7);
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

        ret = write_async (fs, fd, 16);
        if (ret) {
                fprintf (stderr, "write_async failed\n");
        }

        sleep (1);
        ret = glfs_fgetxattr (fd, "trusted.glusterfs.abc", buf, sizeof buf);
        while (cbk_complete != 1) {
                /* ret will be -ve as xattr doesn't exist, and fgetxattr should
                 * return waaaayyy before writev */
                ret = 0;
                sleep (1);
        }
        if (cbk_ret_val < 0) {
                fprintf (stderr, "cbk_ret_val is -ve\n");
                ret = -1;
        }
        glfs_close(fd);

out:
        unlink ("/tmp/ec-fgetxattr.log");
        glfs_fini (fs);

        return ret;
}
