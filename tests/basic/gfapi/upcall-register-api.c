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

int upcall_recv = 0;

void up_async_invalidate (struct glfs_upcall *up_arg, void *data)
{
        struct glfs_upcall_inode *in_arg = NULL;
        enum glfs_upcall_reason   reason = 0;
        struct glfs_object       *object = NULL;
        uint64_t                  flags = 0;
        uint64_t                  expire = 0;

        if (!up_arg)
                return;

        reason = glfs_upcall_get_reason (up_arg);

        /* Expect 'GLFS_INODE_INVALIDATE' upcall event. */

        if (reason == GLFS_UPCALL_INODE_INVALIDATE) {
                in_arg = glfs_upcall_get_event (up_arg);

                object = glfs_upcall_inode_get_object (in_arg);
                flags = glfs_upcall_inode_get_flags (in_arg);
                expire = glfs_upcall_inode_get_expire (in_arg);

                fprintf (stderr, " upcall event type - %d,"
                                 " object(%p), flags(%d), "
                                 " expire_time_attr(%d)\n" ,
                                 reason, object, flags, expire);
                upcall_recv++;
        }

        glfs_free (up_arg);
        return;
}

int perform_io (glfs_t *fs, glfs_t *fs2, int cnt)
{
        glfs_t                   *fs_tmp = NULL;
        glfs_t                   *fs_tmp2 = NULL;
        glfs_fd_t                *fd_tmp = NULL;
        glfs_fd_t                *fd_tmp2 = NULL;
        char                      readbuf[32];
        char                     *writebuf = NULL;
        glfs_fd_t                *fd = NULL;
        glfs_fd_t                *fd2 = NULL;
        char                     *filename = "file_tmp";
        int                       ret = -1;

        if (!fs || !fs2)
                return -1;

        /* Create file from fs and open it from fs2 */
        fd = glfs_creat(fs, filename, O_RDWR|O_SYNC, 0644);
        if (fd <= 0) {
                ret = -1;
                LOG_ERR ("glfs_creat", ret);
        }

        fd2 = glfs_open(fs2, filename, O_SYNC|O_RDWR|O_CREAT);
        if (fd2 <= 0) {
                ret = -1;
                LOG_ERR ("glfs_open-fs2", ret);
        }

        do {
                if (cnt%2) {
                        fd_tmp = fd;
                        fs_tmp = fs;
                        fd_tmp2 = fd2;
                        fs_tmp2 = fs2;
                } else {
                        fd_tmp = fd2;
                        fs_tmp = fs2;
                        fd_tmp2 = fd;
                        fs_tmp2 = fs;
                }

                /* WRITE on fd_tmp */
                writebuf = malloc(10);
                if (writebuf) {
                        memcpy (writebuf, "abcd", 4);
                        ret = glfs_write (fd_tmp, writebuf, 4, 0);
                        if (ret <= 0)   {
                                ret = -1;
                                LOG_ERR ("glfs_write", ret);
                        }
                        free(writebuf);
                } else {
                        fprintf (stderr,
                                 "Could not allocate writebuf\n");
                        return -1;
                }

                /* READ on fd_tmp2 */
                ret = glfs_lseek (fd_tmp2, 0, SEEK_SET);
                LOG_ERR ("glfs_lseek", ret);

                memset (readbuf, 0, sizeof(readbuf));
                ret = glfs_pread (fd_tmp2, readbuf, 4, 0, 0);

                if (ret <= 0) {
                        ret = -1;
                        LOG_ERR ("glfs_pread", ret);
                }

                sleep(2);
        } while (--cnt > 0);

        sleep(2);

        ret = 0;
err:
        glfs_close(fd);

        glfs_close(fd2);

out:
        return ret;
}

int
main (int argc, char *argv[])
{
        glfs_t                   *fs = NULL;
        glfs_t                   *fs2 = NULL;
        int                       ret = 0, i;
        char                     *vol_id  = NULL;
        unsigned int              cnt = 5;
        struct glfs_upcall       *cbk = NULL;
        char                     *logfile = NULL;
        char                     *volname = NULL;
        char                     *hostname = NULL;
        int                      up_events = GLFS_EVENT_ANY;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        /* Initialize fs */
        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("glfs_init", ret);

        /* Intialize fs2 */
        fs2 = glfs_new (volname);
        if (!fs2) {
                fprintf (stderr, "glfs_new fs2: returned NULL\n");
                return 1;
        }

        ret = glfs_set_volfile_server (fs2, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server-fs2", ret);

        ret = glfs_set_logging (fs2, logfile, 7);
        LOG_ERR("glfs_set_logging-fs2", ret);

        ret = glfs_init (fs2);
        LOG_ERR("glfs_init-fs2", ret);

        /* Register Upcalls */
        ret = glfs_upcall_register (fs, up_events, up_async_invalidate, NULL);

        /* Check if the return mask contains the event */
        if (!(ret & GLFS_EVENT_INODE_INVALIDATE)) {
                fprintf (stderr, "glfs_upcall_register return doesnt contain"
                         " upcall event\n");
                return -1;
        }

        ret = glfs_upcall_register (fs2, up_events, up_async_invalidate, NULL);

        /* Check if the return mask contains the event */
        if ((ret < 0) || !(ret & GLFS_EVENT_INODE_INVALIDATE)) {
                fprintf (stderr, "glfs_upcall_register return doesnt contain"
                         " upcall event\n");
                return -1;
        }

        /* Perform I/O */
        ret = perform_io (fs, fs2, cnt);
        LOG_ERR("perform_io", ret);

        if (upcall_recv == 0) {
                fprintf (stderr, "Upcalls are not received.\n");
                ret = -1;
        } else {
                fprintf (stderr, "Received %d upcalls as expected\n",
                         upcall_recv);
                ret = 0;
        }

        sleep(5); /* to flush out previous upcalls if any */

        /* Now unregister and check there are no upcall events received */
        ret = glfs_upcall_unregister (fs, up_events);

        /* Check if the return mask contains the event */
        if ((ret < 0) || !(ret & GLFS_EVENT_INODE_INVALIDATE)) {
                fprintf (stderr, "glfs_upcall_unregister return doesnt contain"
                         " upcall event\n");
                return -1;
        }

        ret = glfs_upcall_unregister (fs2, up_events);

        /* Check if the return mask contains the event */
        if ((ret < 0) || !(ret & GLFS_EVENT_INODE_INVALIDATE)) {
                fprintf (stderr, "glfs_upcall_unregister return doesnt contain"
                         " upcall event\n");
                return -1;
        }

        upcall_recv = 0;

        ret = perform_io (fs, fs2, cnt);
        LOG_ERR("perform_io", ret);

        if (upcall_recv != 0) {
                fprintf (stderr, "%d upcalls received even after unregister.\n",
                         upcall_recv);
                ret = -1;
        } else {
                fprintf (stderr, "Post unregister, no upcalls received as"
                         " expected\n");
                ret = 0;
        }

out:
        if (fs) {
                ret = glfs_fini(fs);
                fprintf (stderr, "glfs_fini(fs) returned %d\n", ret);
        }

        if (fs2) {
                ret = glfs_fini(fs2);
                fprintf (stderr, "glfs_fini(fs2) returned %d\n", ret);
        }

        if (ret)
                exit(1);
        exit(0);
}


