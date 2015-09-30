#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define GETLK_OWNER_CHECK(f, cp, label)                     \
    do {                                                    \
        switch (f.l_type) {                                 \
        case F_RDLCK:                                       \
        case F_WRLCK:                                       \
            ret = 1;                                        \
            goto label;                                     \
        case F_UNLCK:                                       \
            if (!are_flocks_sane (&f, &cp)) {               \
                ret = 1;                                    \
                goto label;                                 \
            }                                               \
            break;                                          \
        }                                                   \
    } while (0)

void
flock_init (struct flock *f, short int type, off_t start, off_t len)
{
    f->l_type = type;
    f->l_start = start;
    f->l_len = len;
}

int
flock_cp (struct flock *dst, struct flock *src)
{
    memcpy ((void *) dst, (void *) src, sizeof (struct flock));
}

int
are_flocks_sane (struct flock *src, struct flock *cpy)
{
    return ((src->l_whence == cpy->l_whence) &&
            (src->l_start == cpy->l_start) &&
            (src->l_len == cpy->l_len));
}

/*
 * Test description:
 * SETLK (0,3), F_WRLCK
 * SETLK (3,3), F_WRLCK
 *
 * the following GETLK requests must return flock struct unmodified
 * except for l_type to F_UNLCK
 * GETLK (3,3), F_WRLCK
 * GETLK (3,3), F_RDLCK
 *
 * */

int main (int argc, char **argv)
{
    int fd = -1;
    int ret = 1;
    char *fname = NULL;
    struct flock f = {0,};
    struct flock cp = {0,};

    if (argc < 2)
        goto out;

    fname = argv[1];
    fd = open (fname, O_RDWR);
    if (fd == -1) {
        perror ("open");
        goto out;
    }

    flock_init (&f, F_WRLCK, 0, 3);
    flock_cp (&cp, &f);
    ret = fcntl (fd, F_SETLK, &f);
    if (ret) {
        perror ("fcntl");
        goto out;
    }
    if (!are_flocks_sane (&f, &cp)) {
        ret = 1;
        goto out;
    }

    flock_init (&f, F_WRLCK, 3, 3);
    flock_cp (&cp, &f);
    ret = fcntl (fd, F_SETLK, &f);
    if (ret) {
        perror ("fcntl");
        goto out;
    }
    if (!are_flocks_sane (&f, &cp)) {
        ret = 1;
        goto out;
    }

    flock_init (&f, F_WRLCK, 3, 3);
    flock_cp (&cp, &f);
    ret = fcntl (fd, F_GETLK, &f);
    if (ret) {
        perror ("fcntl");
        return 1;
    }
    GETLK_OWNER_CHECK (f, cp, out);

    flock_init (&f, F_RDLCK, 3, 3);
    flock_cp (&cp, &f);
    ret = fcntl (fd, F_GETLK, &f);
    if (ret) {
        perror ("fcntl");
        return 1;
    }
    GETLK_OWNER_CHECK (f, cp, out);

out:
    if (fd != -1)
        close (fd);
    return ret;
}
