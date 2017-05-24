#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define LOG_ERR(msg) do { \
        fprintf (stderr, "%s : Error (%s)\n", msg, strerror (errno)); \
        } while (0)

/*This test tests that shard xlator handles offset in appending writes
 * correctly. This test performs writes of 1025 bytes 1025 times, in 5 threads
 * with different threads. The buffer to be written is same character repeated
 * 1025 times in the buffer for a thread. At the end it reads the buffer till
 * end of file and tests that the read of 1025 bytes is always same character
 * and the content read is 5*1025*1025 size. 1025 bytes is chosen because it
 * will lead to write on more than one shard at some point when the size is
 * going over the initial shard*/
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int thread_data = '1';

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

void*
write_data (void *data)
{
        char           buf[1025] = {0};
        glfs_fd_t      *glfd = NULL;
        glfs_t         *fs       = data;
        int            i     = 0;

        pthread_mutex_lock (&lock);
        {
                memset(buf, thread_data, sizeof(buf));
                thread_data++;
        }
        pthread_mutex_unlock (&lock);

        for (i = 0; i < 1025; i++) {
                glfd = glfs_creat(fs, "parallel-write.txt", O_WRONLY | O_APPEND,
                                   S_IRUSR | S_IWUSR | O_SYNC);
                if (!glfd) {
                        LOG_ERR ("Failed to create file");
                        exit(1);
                }

                if (glfs_write (glfd, buf, sizeof(buf), 0) < 0) {
                        LOG_ERR ("Failed to write to file");
                        exit(1);
                }
                if (glfs_close(glfd) != 0) {
                        LOG_ERR ("Failed to close file");
                        exit(1);
                }
        }
        return NULL;
}

int
main (int argc, char *argv[])
{
        pthread_t  tid[5] = {0};
        char       buf[1025] = {0};
        char       cmp_buf[1025] = {0};
        int         ret      = 0;
        char       *hostname = NULL;
        char       *volname  = NULL;
        char       *logfile  = NULL;
        glfs_t     *fs       = NULL;
        glfs_fd_t  *glfd     = NULL;
        ssize_t     bytes_read = 0;
        ssize_t     total_bytes_read = 0;
        int i = 0;

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

        for (i = 0; i < 5; i++) {
                pthread_create(&tid[i], NULL, write_data, fs);
        }

        for (i = 0; i < 5; i++) {
                pthread_join(tid[i], NULL);
        }
        glfd = glfs_open(fs, "parallel-write.txt", O_RDONLY);
        if (!glfd) {
                LOG_ERR ("Failed to open file for reading");
                exit(1);
        }

        while ((bytes_read = glfs_read (glfd, buf, sizeof(buf), 0)) > 0) {
                if (bytes_read != sizeof(buf)) {
                        fprintf (stderr, "Didn't read complete data read: %zd "
                                 "expected: %lu", bytes_read, sizeof(buf));
                        exit(1);
                }

                total_bytes_read += bytes_read;
                if (buf[0] < '1' || buf[0] >= thread_data) {
                        fprintf(stderr, "Invalid character found: %c", buf[0]);
                        exit(1);
                }
                memset(cmp_buf, buf[0], sizeof(cmp_buf));
                if (memcmp(cmp_buf, buf, sizeof(cmp_buf))) {
                        LOG_ERR ("Data corrupted");
                        exit(1);
                }
                memset(cmp_buf, 0, sizeof(cmp_buf));
        }

        if (total_bytes_read != 5*1025*1025) {
                fprintf(stderr, "Failed to read what is written, read; %zd, "
                        "expected %zu", total_bytes_read, 5*1025*1025);
                exit(1);
        }

        if (glfs_close(glfd) != 0) {
                LOG_ERR ("Failed to close");
                exit(1);
        }
        return 0;
}
