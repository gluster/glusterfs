/*
.\"This manpage is Copyright (C) 2015 Anna Schumaker <Anna.Schumaker@Netapp.com>
.\"
.\" %%%LICENSE_START(VERBATIM)
.\" Permission is granted to make and distribute verbatim copies of this
.\" manual provided the copyright notice and this permission notice are
.\" preserved on all copies.
.\"
.\" Permission is granted to copy and distribute modified versions of
.\" this manual under the conditions for verbatim copying, provided that
.\" the entire resulting derived work is distributed under the terms of
.\" a permission notice identical to this one.
.\"
.\" Since the Linux kernel and libraries are constantly changing, this
.\" manual page may be incorrect or out-of-date.  The author(s) assume
.\" no responsibility for errors or omissions, or for damages resulting
.\" from the use of the information contained herein.  The author(s) may
.\" not have taken the same level of care in the production of this
.\" manual, which is licensed free of charge, as they might when working
.\" professionally.
.\"
.\" Formatted or processed versions of this manual, if unaccompanied by
.\" the source, must acknowledge the copyright and authors of this work.
.\" %%%LICENSE_END
.\"
.TH COPY_FILE_RANGE 2 2020-06-09 "Linux" "Linux Programmer's Manual"
*/

/*
 * Based on sample code from copy_file_range(2).
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#if (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27))
/* On versions of glibc before 2.27, we must invoke copy_file_range()
   using syscall(2) */

static loff_t
copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out,
                size_t len, unsigned int flags)
{
    return syscall(__NR_copy_file_range, fd_in, off_in, fd_out, off_out, len,
                   flags);
}
#endif

int
main(int argc, char **argv)
{
    int fd_in, fd_out;
    struct stat stat;
    loff_t len, ret;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source> <destination>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        perror("open (argv[1])");
        exit(EXIT_FAILURE);
    }

    if (fstat(fd_in, &stat) == -1) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }

    len = stat.st_size;

    fd_out = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_out == -1) {
        perror("open (argv[2])");
        exit(EXIT_FAILURE);
    }

    do {
        ret = copy_file_range(fd_in, NULL, fd_out, NULL, len, 0);
        if (ret == -1) {
            if (errno == ENOSYS) {
                exit(2);
            }
            perror("copy_file_range");
            exit(EXIT_FAILURE);
        }

        len -= ret;
    } while (len > 0 && ret > 0);

    close(fd_in);
    close(fd_out);
    exit(EXIT_SUCCESS);
}
