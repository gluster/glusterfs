/*
 * Regression test for https://github.com/gluster/glusterfs/issues/4850
 *
 * A file opened with O_APPEND is mmapped (MAP_SHARED) and written to
 * through the mapping. The kernel sends the writeback with the mapped
 * offset and FUSE_WRITE_CACHE; the VFS does not apply O_APPEND to mmap
 * writeback. On gluster the write used to be appended at EOF (the
 * brick's pwrite() honored the backend fd's O_APPEND), relocating the
 * data and doubling the file size.
 *
 * Expected (same as on a local filesystem): the data lands at the
 * mapped offset 0 and the size stays at the fallocated 16 bytes.
 * Usage: bug-4850-mmap-oappend <dir>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FILE_SIZE 16
#define WRITE_DATA "Test data!"
#define WRITE_SIZE 10
#define READ_SIZE 100

static int failures = 0;

static void
check(int cond, const char *what)
{
    printf("%s: %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        failures++;
}

int
main(int argc, char **argv)
{
    char path[4096];
    char buffer[READ_SIZE + 1] = {0};
    const char *dir = (argc > 1) ? argv[1] : ".";
    int fd, ret, i, zeros_ok;
    void *addr;

    if (snprintf(path, sizeof(path), "%s/bug-4850-file", dir) < 0) {
        fprintf(stderr, "snprintf failed\n");
        return 1;
    }

    fd = creat(path, 0600);
    if (fd < 0) {
        perror("creat");
        return 1;
    }
    close(fd);

    fd = open(path, O_RDWR | O_APPEND);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    ret = posix_fallocate(fd, 0, FILE_SIZE);
    if (ret != 0) {
        fprintf(stderr, "posix_fallocate failed: %s\n", strerror(ret));
        return 1;
    }

    addr = mmap(NULL, WRITE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    memcpy(addr, WRITE_DATA, WRITE_SIZE);
    /* Flush the dirty page before unmapping so the result does not
       depend on writeback timing. fsync() additionally flushes the
       write-behind queue on the gluster side, which ACKs writes
       before fulfilling them. */
    if (msync(addr, WRITE_SIZE, MS_SYNC) != 0) {
        perror("msync");
        return 1;
    }
    if (munmap(addr, WRITE_SIZE) != 0) {
        perror("munmap");
        return 1;
    }
    if (fsync(fd) != 0) {
        perror("fsync");
        return 1;
    }
    close(fd);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("reopen");
        return 1;
    }
    ret = read(fd, buffer, READ_SIZE);
    close(fd);
    if (ret < 0) {
        perror("read");
        unlink(path);
        return 1;
    }

    check(ret == FILE_SIZE, "file size is 16 bytes after mmap write");
    check(memcmp(buffer, WRITE_DATA, WRITE_SIZE) == 0,
          "mmap-written data is at offset 0");
    zeros_ok = 1;
    for (i = WRITE_SIZE; i < FILE_SIZE; i++)
        if (buffer[i] != 0)
            zeros_ok = 0;
    check(zeros_ok, "bytes 10..15 are still zeroes");

    unlink(path);
    printf("RESULT: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
