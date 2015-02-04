#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define LOG_ERR(func, err) do {                                                \
        fprintf (stderr, "%s : returned error (%s)\n", func, strerror(err));   \
        exit (err);                                                            \
} while (0)

int       fd;
struct    flock lock;
char      *buf = "ten bytes!";
char      *fname = "/mnt/glusterfs/0/mand.lock";
int       open_flags, child, err, status, blocked = 0;

int do_child (char *argv[]) {
        /* Initialize file open flags */
        if (strcmp (argv[2], "BLOCK") == 0)
                open_flags = O_RDWR;
        else if (strcmp (argv[2], "TRUNC") == 0)
                open_flags = O_RDWR | O_TRUNC | O_NONBLOCK;
        else if (strcmp (argv[2], "NONE") == 0)
                open_flags = O_RDWR | O_NONBLOCK;
        else
                LOG_ERR ("Invalid option:", EINVAL);

        /* Open the file */
        fd = open (fname, open_flags);
        if (fd == -1)
                LOG_ERR ("Child open", errno);

        /* Perform the file operation*/
        if (strcmp (argv[3], "READ") == 0) {
                buf = NULL;
                err = read (fd, buf, 10);
                if (err == -1)
                        LOG_ERR ("Child read", errno);
        } else if (strcmp (argv[3], "WRITE") == 0) {
                err = write (fd, buf, 10);
                if (err == -1)
                        LOG_ERR ("Child write", errno);
        } else if (strcmp (argv[3], "FTRUNCATE") == 0) {
                err = ftruncate (fd, 5);
                if (err)
                        LOG_ERR ("Child ftruncate", errno);
        } else
                LOG_ERR ("Invalid operation:", EINVAL);

        /* Close child fd */
        err = close (fd);
        if (err)
                LOG_ERR ("Child close", errno);

        /* Exit success */
        exit (0);
}

int main (int argc, char *argv[]) {
        if (argc < 4) {
                fprintf (stderr, "Wrong usage: Use as ./mandatory-lock "
                                "<RD_LCK/WR_LCK> <BLOCK/TRUNC/NONE> "
                                "<READ/WRITE/FTRUNCATE\n");
                exit(EINVAL);
        }
        /* Create an empty lock file */
        fd = open (fname, O_CREAT | O_RDWR, 0755);
        if (fd == -1)
                LOG_ERR ("Parent create", errno);

        /* Determine the type of lock */
        if (strcmp (argv[1], "RD_LCK") == 0)
                lock.l_type = F_RDLCK;
        else if (strcmp (argv[1], "WR_LCK") == 0)
                lock.l_type = F_WRLCK;
        else
                LOG_ERR ("Parent lock type", EINVAL);

        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 0L;

        /* Let parent acquire the initial lock */
        err = fcntl (fd, F_SETLK, &lock);
        if (err)
                LOG_ERR ("Parent lock", errno);

        /* Now fork a child */
        child = fork ();
        if (child == 0)
                /* Perform the child operations */
                do_child (argv);
        else {
                /* If blocking mode, then sleep for 2 seconds
                 * and wait for the child */
                if (strcmp (argv[2], "NONE") != 0) {
                        sleep (2);
                        if (waitpid (child, &status, WNOHANG) == 0)
                                blocked = 1;
                        /* Release the parent lock so that the
                         * child can terminate */
                        lock.l_type = F_UNLCK;
                        err = fcntl (fd, F_SETLK, &lock);
                        if (err)
                                LOG_ERR ("Parent unlock", errno);
                }

                /* Wait for child to finish */
                waitpid (child, &status, 0);

                /* Close the parent fd */
                err = close (fd);
                if (err)
                        LOG_ERR ("Parent close", errno);

                /* Remove the lock file*/
                err = unlink (fname);
                if (err)
                        LOG_ERR ("Parent unlink", errno);

                /* If not blocked, exit with child exit status*/
                errno = WEXITSTATUS(status);

                /* If blocked, exit with corresponding
                 * error code */
                if (blocked)
                        errno = EWOULDBLOCK;

                if (errno != 0)
                        printf ("%s\n", strerror(errno));

                exit (errno);

        }
}
