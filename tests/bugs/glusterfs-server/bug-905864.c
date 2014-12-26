#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>


pthread_t   th[5] = {0};
void
flock_init (struct flock *f, short int type, off_t start, off_t len)
{
    f->l_type = type;
    f->l_start = start;
    f->l_len = len;
}

int
flock_range_in_steps (int fd, int is_set, short l_type,
                      int start, int end, int step)
{
        int         ret = 0;
        int         i = 0;
        struct      flock f = {0,};

        for (i = start; i+step < end; i += step) {
                flock_init (&f, l_type, i, step);
                ret = fcntl (fd, (is_set) ? F_SETLKW : F_GETLK, &f);
                if (ret) {
                        perror ("fcntl");
                        goto out;
                }
        }
out:
        return ret;
}

void *
random_locker (void *arg)
{
        int         fd = *(int *)arg;
        int         i = 0;
        int         is_set = 0;

        /* use thread id to choose GETLK or SETLK operation*/
        is_set = pthread_self () % 2;
        (void)flock_range_in_steps (fd, is_set, F_WRLCK, 0, 400, 1);

        return NULL;
}


int main (int argc, char **argv)
{
        int         fd = -1;
        int         ret = 1;
        int         i = 0;
        char        *fname = NULL;

        if (argc < 2)
                goto out;

        fname = argv[1];
        fd = open (fname, O_RDWR);
        if (fd == -1) {
                perror ("open");
                goto out;
        }

        ret = flock_range_in_steps (fd, 1, F_WRLCK, 0, 2000, 2);
        for (i = 0; i < 5; i++) {
                pthread_create (&th[i], NULL, random_locker, (void *) &fd);
        }
        ret = flock_range_in_steps (fd, 1, F_WRLCK, 0, 2000, 2);
        for (i = 0; i < 5; i++) {
                pthread_join (th[i], NULL);
        }
out:
        if (fd != -1)
                close (fd);

        return ret;
}
