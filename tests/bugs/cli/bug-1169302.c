#include <errno.h>
#include <stdio.h>
#include <signal.h>

#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

int keep_running = 1;

void stop_running(int sig)
{
        if (sig == SIGTERM)
                keep_running = 0;
}

int
main (int argc, char *argv[])
{
        glfs_t *fs = NULL;
        int ret = 0;
        glfs_fd_t *fd = NULL;
        char *filename = NULL;
        char *logfile = NULL;
        char *host = NULL;

        if (argc != 5) {
                return -1;
        }

        host = argv[2];
        logfile = argv[3];
        filename = argv[4];

        /* setup signal handler for exiting */
        signal (SIGTERM, stop_running);

        fs = glfs_new (argv[1]);
        if (!fs) {
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", host, 24007);
        if (ret < 0) {
                return -1;
        }

        ret = glfs_set_logging (fs, logfile, 7);
        if (ret < 0) {
                return -1;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                return -1;
        }

        fd = glfs_creat (fs, filename, O_RDWR, 0644);
        if (!fd) {
                return -1;
        }

        /* sleep until SIGTERM has been received */
        while (keep_running) {
                sleep (1);
        }

        ret = glfs_close (fd);
        if (ret < 0) {
                return -1;
        }

        ret = glfs_fini (fs);
        if (ret < 0) {
                return -1;
        }

        return 0;
}
