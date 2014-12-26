#include <sys/file.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>

#ifndef linux
#define fstat64(fd, st) fstat(fd, st)
#endif

int
run_child (char *filename)
{
        int          fd   = -1, ret = -1;
        struct flock lock = {0, };
        int          ppid = 0;

        fd = open (filename, O_RDWR);
        if (fd < 0) {
                fprintf (stderr, "open failed (%s)\n", strerror (errno));
                goto out;
        }

        ppid = getppid ();

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        ret = fcntl (fd, F_GETLK, &lock);
        if (ret < 0) {
                fprintf (stderr, "GETLK failed (%s)\n", strerror (errno));
                goto out;
        }

        if ((lock.l_type == F_UNLCK) ||
            (ppid != lock.l_pid)) {
                fprintf (stderr, "no locks present, though parent has held "
                         "one\n");
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
main (int argc, char *argv[])
{
        int          fd       = -1, ret = -1, status = 0;
        char        *filename = NULL, *cmd = NULL;
        struct stat  stbuf    = {0, };
        struct flock lock     = {0, };

        if (argc != 3) {
                fprintf (stderr, "Usage: %s <filename> "
                         "<gluster-cmd-to-trigger-graph-switch>\n", argv[0]);
                goto out;
        }

        filename = argv[1];
        cmd = argv[2];

        fd = open (filename, O_RDWR | O_CREAT, 0);
        if (fd < 0) {
                fprintf (stderr, "open (%s) failed (%s)\n", filename,
                         strerror (errno));
                goto out;
        }

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        ret = fcntl (fd, F_SETLK, &lock);
        if (ret < 0) {
                fprintf (stderr, "fcntl failed (%s)\n", strerror (errno));
                goto out;
        }

        system (cmd);

        /* wait till graph switch completes */
        ret = fstat64 (fd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat64 failure (%s)\n", strerror (errno));
                goto out;
        }

        sleep (10);

        /* By now old-graph would be disconnected and locks should be cleaned
         * up if they are not migrated. Check that by trying to acquire a lock
         * on a new fd opened by another process on same file.
         */
        ret = fork ();
        if (ret == 0) {
                ret = run_child (filename);
        } else {
                wait (&status);
                if (WIFEXITED(status)) {
                        ret = WEXITSTATUS(status);
                } else {
                        ret = 0;
                }
        }

out:
        return ret;
}
