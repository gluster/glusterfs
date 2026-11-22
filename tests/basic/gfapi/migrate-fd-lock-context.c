/*
 * migrate-fd-lock-context.c -- a gfapi fd that holds a POSIX lock across a
 * graph switch must keep that lock visible to the new graph's
 * protocol/client, and must not leak a lock context.
 *
 * Regression test for glfs_migrate_fd_safe(): it replaced the new fd's lock
 * context by the old fd's only after opening the new fd on the new graph,
 * and the open is when protocol/client takes the reference it consults for
 * client.strict-locks and for reopening the fd. The new graph's client kept
 * the new fd's own, empty context (it believed the fd held no lock), and
 * that context leaked when the fd was closed.
 *
 * The program creates a file, takes a write lock on it, changes a
 * client-side volume option through the command given on the command line
 * and calls glfs_fstat() until its own log shows the client switched to the
 * new graph (the open fds migrate at that point). It then takes a statedump
 * (the .t checks that the new graph's client lists the lock), releases the
 * lock, closes the file and takes a second statedump (the .t checks that no
 * lock context is left). A run where the switch did not happen is reported
 * as inconclusive (exit 3), never as a pass.
 *
 * Usage: migrate-fd-lock-context <host> <volume> <logfile> <pidfile>
 *                                <command>...
 *        <command>... is run through the shell (joined by spaces) to
 *        change a client-side volume option.
 * Exit status 0 iff every step succeeded.
 */

#include <glusterfs/api/glfs.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_FILE "/migrate-fd-lock-context.dat"
#define SWITCHED "switched to graph"
#define SWITCH_POLL_US 250000
#define SWITCH_POLLS 80 /* 20 s */

/* how many lines of the client log contain the needle */
static long
count_log_lines(const char *path, const char *needle)
{
    FILE *fp = fopen(path, "r");
    char line[8192];
    long n = 0;

    if (!fp)
        return 0;
    while (fgets(line, sizeof(line), fp))
        if (strstr(line, needle))
            n++;
    fclose(fp);
    return n;
}

static int
set_lock(glfs_fd_t *fd, short type)
{
    struct flock fl = {
        .l_type = type,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
    };

    return glfs_posix_lock(fd, F_SETLK, &fl);
}

int
main(int argc, char *argv[])
{
    glfs_t *fs = NULL;
    glfs_fd_t *fd = NULL;
    struct stat st;
    FILE *pf = NULL;
    char cmd[4096] = "";
    long before = 0;
    int i = 0, switched = 0, ret = 0;

    if (argc < 6) {
        fprintf(stderr,
                "usage: %s <host> <volume> <logfile> <pidfile> <command>...\n",
                argv[0]);
        return 1;
    }
    for (i = 5; i < argc; i++) {
        if (strlen(cmd) + strlen(argv[i]) + 2 > sizeof(cmd)) {
            fprintf(stderr, "command too long\n");
            return 1;
        }
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }

    fs = glfs_new(argv[2]);
    if (!fs) {
        fprintf(stderr, "glfs_new: %s\n", strerror(errno));
        return 1;
    }
    if (glfs_set_volfile_server(fs, "tcp", argv[1], 24007) ||
        glfs_set_logging(fs, argv[3], 7) || glfs_init(fs)) {
        fprintf(stderr, "glfs setup: %s\n", strerror(errno));
        return 1;
    }

    fd = glfs_creat(fs, TEST_FILE, O_RDWR, 0644);
    if (!fd) {
        fprintf(stderr, "glfs_creat: %s\n", strerror(errno));
        return 1;
    }
    if (set_lock(fd, F_WRLCK)) {
        fprintf(stderr, "glfs_posix_lock(F_WRLCK): %s\n", strerror(errno));
        return 1;
    }

    before = count_log_lines(argv[3], SWITCHED);
    ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "'%s' failed (%d)\n", cmd, ret);
        return 1;
    }
    for (i = 0; i < SWITCH_POLLS; i++) {
        if (glfs_fstat(fd, &st)) {
            fprintf(stderr, "glfs_fstat: %s\n", strerror(errno));
            return 1;
        }
        if (count_log_lines(argv[3], SWITCHED) > before) {
            switched = 1;
            break;
        }
        usleep(SWITCH_POLL_US);
    }
    if (!switched) {
        fprintf(stderr, "inconclusive: the client never switched graphs\n");
        return 3;
    }
    /* one call on the handle after the switch was logged */
    if (glfs_fstat(fd, &st)) {
        fprintf(stderr, "glfs_fstat after the switch: %s\n", strerror(errno));
        return 1;
    }

    pf = fopen(argv[4], "w");
    if (!pf) {
        fprintf(stderr, "fopen(%s): %s\n", argv[4], strerror(errno));
        return 1;
    }
    fprintf(pf, "%d\n", (int)getpid());
    fclose(pf);

    /* first statedump: the lock is held, the fd lives on the new graph */
    if (glfs_sysrq(fs, GLFS_SYSRQ_STATEDUMP)) {
        fprintf(stderr, "glfs_sysrq: %s\n", strerror(errno));
        return 1;
    }

    if (set_lock(fd, F_UNLCK)) {
        fprintf(stderr, "glfs_posix_lock(F_UNLCK): %s\n", strerror(errno));
        return 1;
    }
    if (glfs_close(fd)) {
        fprintf(stderr, "glfs_close: %s\n", strerror(errno));
        return 1;
    }

    /* statedump names carry the time in seconds */
    sleep(2);
    if (glfs_sysrq(fs, GLFS_SYSRQ_STATEDUMP)) {
        fprintf(stderr, "glfs_sysrq: %s\n", strerror(errno));
        return 1;
    }

    glfs_fini(fs);
    return 0;
}
