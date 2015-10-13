#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>


void usage (void)
{

        printf ("Usage: testlock <filepath> [R|W]\n");
        return;
}


int main (int argc, char *argv[])
{
        char *file_path = NULL;
        int fd = -1;
        struct flock lock = {0};
        int ret = -1;
        int c = 0;

        if (argc != 3) {
                usage ();
                exit (1);
        }

        file_path = argv[1];
        fd = open (file_path, O_RDWR);

        if (-1 == fd) {
                printf ("Failed to open file %s. %m\n", file_path);
                exit (1);
        }

        /* TODO: Check for invalid input*/

        if (!strcmp (argv[2], "W")) {
                lock.l_type = F_WRLCK;
                printf("Taking write lock\n");

        } else {
                lock.l_type = F_RDLCK;
                printf("Taking read lock\n");
        }

        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        lock.l_pid = getpid ();


        printf ("Acquiring lock on %s\n", file_path);
        ret = fcntl (fd, F_SETLK, &lock);
        if (ret) {
                printf ("Failed to acquire lock on %s (%m)\n", file_path);
                close (fd);
                exit (1);
        }

        sleep(10);

        /*Unlock*/

        printf ("Releasing lock on %s\n", file_path);
        lock.l_type = F_UNLCK;
        ret = fcntl (fd, F_SETLK, &lock);
        if (ret) {
                printf ("Failed to release lock on %s (%m)\n", file_path);
        }

        close (fd);
        return ret;

}
