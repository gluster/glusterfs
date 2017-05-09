
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static char buffer[65536];

static int
parse_int(const char *text, size_t *value)
{
        char *ptr;
        size_t val;

        val = strtoul(text, &ptr, 0);
        if (*ptr != 0) {
                return 0;
        }

        *value = val;

        return 1;
}

static int
fill_area(int fd, off_t offset, size_t size)
{
        size_t len;
        ssize_t res;

        while (size > 0) {
                len = sizeof(buffer);
                if (len > size) {
                        len = size;
                }
                res = pwrite(fd, buffer, len, offset);
                if (res < 0) {
                        fprintf(stderr,
                                "pwrite(%d, %p, %lu, %lu) failed: %d\n",
                                fd, buffer, size, offset, errno);
                        return 0;
                }
                if (res != len) {
                        fprintf(stderr,
                                "pwrite(%d, %p, %lu, %lu) didn't wrote all "
                                "data: %lu/%lu\n",
                                fd, buffer, size, offset, res, len);
                        return 0;
                }
                offset += len;
                size -= len;
        }

        return 1;
}

static void
syntax(void)
{
        fprintf(stderr, "Syntax: seek create <path> <offset> <size> [...]\n");
        fprintf(stderr, "        seek scan <path> data|hole <offset>\n");
}

static int
seek_create(const char *path, int argc, char *argv[])
{
        size_t off, size;
        int fd;
        int ret = 1;

        fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (fd < 0) {
                fprintf(stderr, "Failed to create the file\n");
                goto out;
        }

        while (argc > 0) {
                if (!parse_int(argv[0], &off) ||
                    !parse_int(argv[1], &size)) {
                        syntax();
                        goto out_close;
                }
                if (!fill_area(fd, off, size)) {
                        goto out_close;
                }
                argv += 2;
                argc -= 2;
        }

        ret = 0;

out_close:
        close(fd);
out:
        return ret;
}

static int
seek_scan(const char *path, const char *type, const char *pos)
{
        size_t off, res;
        int fd, whence;
        int ret = 1;

        if (strcmp(type, "data") == 0) {
                whence = SEEK_DATA;
        } else if (strcmp(type, "hole") == 0) {
                whence = SEEK_HOLE;
        } else {
                syntax();
                goto out;
        }

        if (!parse_int(pos, &off)) {
                syntax();
                goto out;
        }

        fd = open(path, O_RDWR);
        if (fd < 0) {
                fprintf(stderr, "Failed to open the file\n");
                goto out;
        }

        res = lseek(fd, off, whence);
        if (res == (off_t)-1) {
                if (errno != ENXIO) {
                        fprintf(stderr, "seek(%d, %lu, %d) failed: %d\n", fd,
                                off, whence, errno);
                        goto out_close;
                }
                fprintf(stdout, "ENXIO\n");
        } else {
                fprintf(stdout, "%lu\n", res);
        }

        ret = 0;

out_close:
        close(fd);
out:
        return ret;
}

int
main(int argc, char *argv[])
{
        int ret = 1;

        memset(buffer, 0x55, sizeof(buffer));

        if (argc < 3) {
                syntax();
                goto out;
        }

        if (strcmp(argv[1], "create") == 0) {
                if (((argc - 3) & 1) != 0) {
                        syntax();
                        goto out;
                }
                ret = seek_create(argv[2], argc - 3, argv + 3);
        } else if (strcmp(argv[1], "scan") == 0) {
                if (argc != 5) {
                        syntax();
                        goto out;
                }
                ret = seek_scan(argv[2], argv[3], argv[4]);
        } else {
                syntax();
                goto out;
        }

        ret = 0;

out:
        return ret;
}

