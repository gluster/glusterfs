
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include <glusterfs/api/glfs.h>

/* Based on a reproducer by Stefan Ring. It seems to be quite sensible to any
 * timing modification, so the code has been maintained as is, only with minor
 * changes. */

struct glfs *glfs;

pthread_mutex_t the_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t the_cond = PTHREAD_COND_INITIALIZER;

typedef struct _my_aiocb {
    int64_t size;
    volatile int64_t seq;
    int which;
} my_aiocb;

typedef struct _worker_data {
    my_aiocb cb;
    struct iovec iov;
    int64_t offset;
} worker_data;

typedef struct {
    worker_data wdata[2];

    volatile unsigned busy;
} all_data_t;

all_data_t all_data;

static void
completion_fnc(struct glfs_fd *fd, ssize_t ret, struct glfs_stat *pre,
               struct glfs_stat *post, void *arg)
{
    void *the_thread;
    my_aiocb *cb = (my_aiocb *)arg;
    long seq = cb->seq;

    assert(ret == cb->size);

    pthread_mutex_lock(&the_mutex);
    pthread_cond_broadcast(&the_cond);

    all_data.busy &= ~(1 << cb->which);
    cb->seq = -1;

    the_thread = (void *)pthread_self();
    printf("worker %d is done from thread %p, seq %ld!\n", cb->which,
           the_thread, seq);

    pthread_mutex_unlock(&the_mutex);
}

static void
init_wdata(worker_data *data, int which)
{
    data->cb.which = which;
    data->cb.seq = -1;

    data->iov.iov_base = malloc(1024 * 1024);
    memset(data->iov.iov_base, 6,
           1024 * 1024); /* tail part never overwritten */
}

static void
init()
{
    all_data.busy = 0;

    init_wdata(&all_data.wdata[0], 0);
    init_wdata(&all_data.wdata[1], 1);
}

static void
do_write(struct glfs_fd *fd, int content, int size, int64_t seq,
         worker_data *wdata, const char *name)
{
    int ret;

    wdata->cb.size = size;
    wdata->cb.seq = seq;

    if (content >= 0)
        memset(wdata->iov.iov_base, content, size);
    wdata->iov.iov_len = size;

    pthread_mutex_lock(&the_mutex);
    printf("(%d) dispatching write \"%s\", offset %lx, len %x, seq %ld\n",
           wdata->cb.which, name, (long)wdata->offset, size, (long)seq);
    pthread_mutex_unlock(&the_mutex);
    ret = glfs_pwritev_async(fd, &wdata->iov, 1, wdata->offset, 0,
                             completion_fnc, &wdata->cb);
    assert(ret >= 0);
}

#define IDLE 0  // both workers must be idle
#define ANY 1   // use any worker, other one may be busy

int
get_worker(int waitfor, int64_t excl_seq)
{
    int which;

    pthread_mutex_lock(&the_mutex);

    while (waitfor == IDLE && (all_data.busy & 3) != 0 ||
           waitfor == ANY &&
               ((all_data.busy & 3) == 3 ||
                excl_seq >= 0 && (all_data.wdata[0].cb.seq == excl_seq ||
                                  all_data.wdata[1].cb.seq == excl_seq)))
        pthread_cond_wait(&the_cond, &the_mutex);

    if (!(all_data.busy & 1))
        which = 0;
    else
        which = 1;

    all_data.busy |= (1 << which);

    pthread_mutex_unlock(&the_mutex);

    return which;
}

static int
doit(struct glfs_fd *fd)
{
    int ret;
    int64_t seq = 0;
    int64_t offset = 0;     // position in file, in blocks
    int64_t base = 0x1000;  // where to place the data, in blocks

    int async_mode = ANY;

    init();

    for (;;) {
        int which;
        worker_data *wdata;

        // for growing to the first offset
        for (;;) {
            int gap = base + 0x42 - offset;
            if (!gap)
                break;
            if (gap > 80)
                gap = 80;

            which = get_worker(IDLE, -1);
            wdata = &all_data.wdata[which];

            wdata->offset = offset << 9;
            do_write(fd, 0, gap << 9, seq++, wdata, "gap-filling");

            offset += gap;
        }

        // 8700
        which = get_worker(IDLE, -1);
        wdata = &all_data.wdata[which];

        wdata->offset = (base + 0x42) << 9;
        do_write(fd, 1, 62 << 9, seq++, wdata, "!8700");

        // 8701
        which = get_worker(IDLE, -1);
        wdata = &all_data.wdata[which];

        wdata->offset = (base + 0x42) << 9;
        do_write(fd, 2, 55 << 9, seq++, wdata, "!8701");

        // 8702
        which = get_worker(async_mode, -1);
        wdata = &all_data.wdata[which];

        wdata->offset = (base + 0x79) << 9;
        do_write(fd, 3, 54 << 9, seq++, wdata, "!8702");

        // 8703
        which = get_worker(async_mode, -1);
        wdata = &all_data.wdata[which];

        wdata->offset = (base + 0xaf) << 9;
        do_write(fd, 4, 81 << 9, seq++, wdata, "!8703");

        // 8704
        // this writes both 5s and 6s
        // the range of 5s is the one that overwrites 8703

        which = get_worker(async_mode, seq - 1);
        wdata = &all_data.wdata[which];

        memset(wdata->iov.iov_base, 5, 81 << 9);
        wdata->offset = (base + 0xaf) << 9;
        do_write(fd, -1, 1623 << 9, seq++, wdata, "!8704");

        offset = base + 0x706;
        base += 0x1000;
        if (base >= 0x100000)
            break;
    }

    printf("done!\n");
    fflush(stdout);

    pthread_mutex_lock(&the_mutex);

    while ((all_data.busy & 3) != 0)
        pthread_cond_wait(&the_cond, &the_mutex);

    pthread_mutex_unlock(&the_mutex);

    ret = glfs_close(fd);
    assert(ret >= 0);
    /*
        ret = glfs_fini(glfs);
        assert(ret >= 0);
    */
    return 0;
}

int
main(int argc, char *argv[])
{
    int ret;
    int open_flags = O_RDWR | O_DIRECT | O_TRUNC;
    struct glfs_fd *fd;

    glfs = glfs_new(argv[1]);
    if (!glfs) {
        printf("glfs_new!\n");
        goto out;
    }
    ret = glfs_set_volfile_server(glfs, "tcp", "localhost", 24007);
    if (ret < 0) {
        printf("set_volfile!\n");
        goto out;
    }
    ret = glfs_init(glfs);
    if (ret) {
        printf("init!\n");
        goto out;
    }
    fd = glfs_open(glfs, argv[2], open_flags);
    if (!fd) {
        printf("open!\n");
        goto out;
    }
    srand(time(NULL));
    return doit(fd);
out:
    return 1;
}
