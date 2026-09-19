/*
 * gfapi-async-graph-switch.c -- an asynchronous fop that completes after a
 * graph switch must release the fd_t it was wound on, not the handle's
 * current fd_t.
 *
 * Regression test for glfs_io_async_cbk(): the six asynchronous fops wind
 * on the fd_t glfs_resolve_fd() returned, and the completion used to release
 * glfd->fd instead. When a graph switch completed while the fop was in
 * flight, __glfs_migrate_openfds() had swapped glfd->fd to the new graph's
 * fd_t, so the completion dropped the handle's own reference on the new fd_t
 * (leaving the handle dead: EBADF on the next read and on close) and leaked
 * the reference it held on the old one.
 *
 * The brick holds READ for several seconds (debug.delay-gen, armed by the
 * .t); the program submits one asynchronous read, changes a client-side
 * volume option through the command given on the command line, and calls
 * glfs_stat() until its own log shows the client switched to the new graph
 * (that is the point where the handle's fd_t is swapped). Once the read has
 * completed, a synchronous read and the close on the same handle must
 * succeed. A run where the switch did not complete before the read did is
 * reported as inconclusive (exit 3), never as a pass.
 *
 * Usage: gfapi-async-graph-switch <host> <volume> <logfile> <command>...
 *        <command>... is run through the shell (joined by spaces) to
 *        change a client-side volume option.
 * Exit status 0 iff the handle survives the completion.
 */

#include <glusterfs/api/glfs.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_FILE "/gfapi-async-graph-switch.dat"
#define FILE_SIZE (64 * 1024)
#define READ_SIZE 4096
#define SWITCHED "switched to graph"
#define ADOPT_POLL_US 200000
#define ADOPT_POLLS 40 /* 8 s: the brick holds the read for 10 s */
#define COMPLETION_POLL_US 100000
#define COMPLETION_POLLS 600 /* 60 s */

static volatile int completed;
static ssize_t completed_ret = -1;
static int completed_errno;

static void
read_cbk(glfs_fd_t *fd, ssize_t ret, struct glfs_stat *prestat,
         struct glfs_stat *poststat, void *data)
{
    (void)fd;
    (void)prestat;
    (void)poststat;
    (void)data;

    completed_ret = ret;
    completed_errno = errno;
    __sync_synchronize();
    completed = 1;
}

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

int
main(int argc, char **argv)
{
    glfs_t *fs = NULL;
    glfs_fd_t *fd = NULL;
    struct stat st;
    static char buf[FILE_SIZE];
    const char *host, *volume, *logfile;
    char *switch_cmd = NULL;
    size_t len = 0;
    long switches, i;
    int adopted = 0;
    int ret;

    if (argc < 5) {
        fprintf(stderr, "usage: %s <host> <volume> <logfile> <command>...\n",
                argv[0]);
        return 2;
    }
    host = argv[1];
    volume = argv[2];
    logfile = argv[3];

    /* the command may arrive word-split by the test harness: join it back */
    for (i = 4; i < argc; i++)
        len += strlen(argv[i]) + 1;
    switch_cmd = calloc(1, len + 1);
    if (!switch_cmd) {
        fprintf(stderr, "calloc: %s\n", strerror(errno));
        return 1;
    }
    for (i = 4; i < argc; i++) {
        strcat(switch_cmd, argv[i]);
        if (i + 1 < argc)
            strcat(switch_cmd, " ");
    }

    fs = glfs_new(volume);
    if (!fs) {
        fprintf(stderr, "glfs_new: %s\n", strerror(errno));
        return 1;
    }
    ret = glfs_set_volfile_server(fs, "tcp", host, 24007);
    ret = ret ?: glfs_set_logging(fs, logfile, 7);
    ret = ret ?: glfs_init(fs);
    if (ret) {
        fprintf(stderr, "glfs_init: %s\n", strerror(errno));
        return 1;
    }

    /* a file large enough that the read is not answered from the lookup */
    fd = glfs_creat(fs, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (!fd) {
        fprintf(stderr, "glfs_creat: %s\n", strerror(errno));
        return 1;
    }
    memset(buf, 'g', sizeof(buf));
    if (glfs_write(fd, buf, sizeof(buf), 0) != (ssize_t)sizeof(buf)) {
        fprintf(stderr, "glfs_write: %s\n", strerror(errno));
        return 1;
    }
    if (glfs_close(fd)) {
        fprintf(stderr, "glfs_close after write: %s\n", strerror(errno));
        return 1;
    }

    fd = glfs_open(fs, TEST_FILE, O_RDWR);
    if (!fd) {
        fprintf(stderr, "glfs_open: %s\n", strerror(errno));
        return 1;
    }

    /* 1. the read is wound on this graph's fd_t and held by the brick */
    switches = count_log_lines(logfile, SWITCHED);
    ret = glfs_pread_async(fd, buf, READ_SIZE, 0, 0, read_cbk, NULL);
    if (ret) {
        fprintf(stderr, "glfs_pread_async: %s\n", strerror(errno));
        return 1;
    }

    /* 2. change the client graph while the read is in flight */
    ret = system(switch_cmd);
    if (ret) {
        fprintf(stderr, "volume set failed: rc=%d\n", ret);
        return 1;
    }

    /* 3. adopt the new graph: the first call after it is ready migrates the
     * open handles and logs the switch */
    for (i = 0; i < ADOPT_POLLS && !completed; i++) {
        usleep(ADOPT_POLL_US);
        (void)glfs_stat(fs, "/", &st);
        if (count_log_lines(logfile, SWITCHED) > switches) {
            adopted = 1;
            break;
        }
    }
    if (!adopted || completed) {
        fprintf(stderr,
                "inconclusive: the graph switch did not complete before the "
                "read did (adopted=%d completed=%d); raise "
                "delay-gen.delay-duration\n",
                adopted, completed);
        return 3;
    }

    /* 4. the completion must release the old graph's fd_t, not ours */
    for (i = 0; i < COMPLETION_POLLS && !completed; i++)
        usleep(COMPLETION_POLL_US);
    if (!completed || completed_ret != READ_SIZE) {
        fprintf(stderr, "asynchronous read: completed=%d ret=%zd errno=%d\n",
                completed, completed_ret, completed_errno);
        return 4;
    }

    /* 5. the handle must still be usable */
    ret = glfs_pread(fd, buf, READ_SIZE, 0, 0, NULL);
    if (ret != READ_SIZE) {
        fprintf(stderr,
                "synchronous read after the switch: ret=%d errno=%d (%s)\n",
                ret, errno, strerror(errno));
        return 5;
    }
    ret = glfs_close(fd);
    if (ret) {
        fprintf(stderr, "glfs_close after the switch: ret=%d errno=%d (%s)\n",
                ret, errno, strerror(errno));
        return 6;
    }

    glfs_fini(fs);
    free(switch_cmd);
    return 0;
}
