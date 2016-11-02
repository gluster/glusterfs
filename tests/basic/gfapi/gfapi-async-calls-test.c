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

#define LOG_ERR(msg) do { \
        fprintf (stderr, "%s : Error (%s)\n", msg, strerror (errno)); \
        } while (0)

int cbk_complete = 0;
int cbk_ret_val = -1;

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

glfs_t *
init_glfs (const char *hostname, const char *volname,
           const char *logfile)
{
        int     ret     = -1;
        glfs_t *fs      = NULL;

        fs = glfs_new (volname);
        if (!fs) {
                LOG_ERR ("glfs_new failed");
                return NULL;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        if (ret < 0) {
                LOG_ERR ("glfs_set_volfile_server failed");
                goto out;
        }

        ret = glfs_set_logging (fs, logfile, 7);
        if (ret < 0) {
                LOG_ERR ("glfs_set_logging failed");
                goto out;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                LOG_ERR ("glfs_init failed");
                goto out;
        }

        ret = 0;
out:
        if (ret) {
                glfs_fini (fs);
                fs = NULL;
        }

        return fs;
}

void
write_async_cbk (glfs_fd_t *fd, ssize_t ret, void *cookie)
{

        if (ret < 0) {
                LOG_ERR ("glfs_write failed");
        }
        cbk_ret_val = ret;
        cbk_complete = 1;
}

int
write_async (glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
        ssize_t         ret          = -1;
        int             flags        = O_RDWR;
        const char     *buff         = "This is from my prog\n";
        struct iovec    iov          = {0};
        void           *write_cookie = NULL;
        void           *read_cookie  = NULL;



        ret = fill_iov (&iov, 'a', char_count);
        if (ret) {
                LOG_ERR ("failed to create iov");
                goto out;
        }

        write_cookie = strdup ("write_cookie");
        ret = glfs_pwritev_async (glfd, &iov, 1, 0, flags, write_async_cbk,
                                  &write_cookie);
out:
        if (ret < 0) {
                LOG_ERR ("glfs_pwritev async failed");
        }
        return ret;

}

int
main (int argc, char *argv[])
{
        int         ret      = 0;
        char       *hostname = NULL;
        char       *volname  = NULL;
        char       *logfile  = NULL;
        glfs_t     *fs       = NULL;
        const char *filename = "glfs_test.txt";
        int         flags    = (O_RDWR|O_CREAT);
        glfs_fd_t  *glfd     = NULL;
        int         count    = 200;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = init_glfs (hostname, volname, logfile);
        if (fs == NULL) {
                LOG_ERR ("init_glfs failed");
                return -1;
        }

        glfd = glfs_creat (fs, filename, flags, 0644);
        if (glfd == NULL) {
                LOG_ERR ("glfs_creat failed");
                exit(1);
        }

        ret = write_async (fs, glfd, count);
        if (ret) {
                LOG_ERR ("glfs_test_function failed");
                exit(1);
        }

        while (cbk_complete != 1) {
                sleep(1);
        }

        ret = glfs_close (glfd);
        if (ret < 0) {
                LOG_ERR ("glfs close  failed");
        }

        /*
         * skipping fini
         */

        if (cbk_ret_val == count)
                return 0;
        else
                return -1;
}


