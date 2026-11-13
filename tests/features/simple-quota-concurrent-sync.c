/* Exercise the production persistence function with a controllable backend. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/logging.h>
#include <glusterfs/syncop.h>
#include <glusterfs/defaults.h>

static dict_t *
test_dict_new(void);
static int
test_dict_set_int64(dict_t *, char *, int64_t);
static void
test_dict_unref(dict_t *);
static inode_t *
test_inode_ref(inode_t *);
static inode_t *
test_inode_unref(inode_t *);
static int
test_setxattr(xlator_t *, loc_t *, dict_t *, int32_t, dict_t *, dict_t **);

#define dict_new test_dict_new
#define dict_set_int64 test_dict_set_int64
#define dict_unref test_dict_unref
#define inode_ref test_inode_ref
#define inode_unref test_inode_unref
#define syncop_setxattr test_setxattr
#undef gf_msg
#undef gf_msg_debug
#undef gf_log
#define gf_msg(...) ((void)0)
#define gf_msg_debug(...) ((void)0)
#define gf_log(...) ((void)0)

/* Keep the synchronization and error handling under test in production code. */
#include "../../xlators/features/simple-quota/src/simple-quota.c"

struct test_dict {
    dict_t dict;
    int64_t value;
};

static pthread_mutex_t backend_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t backend_cond = PTHREAD_COND_INITIALIZER;
static int entered;
static int release_first;
static int release_second;
static int fail_backend;
static int64_t persisted;
static xlator_t translator;
static sq_private_t private;
static sq_inode_t quota;
static inode_t namespace;
static xlator_list_t child;

static dict_t *
test_dict_new(void)
{
    return (dict_t *)calloc(1, sizeof(struct test_dict));
}

static int
test_dict_set_int64(dict_t *dict, char *key, int64_t value)
{
    ((struct test_dict *)dict)->value = value;
    return 0;
}

static void
test_dict_unref(dict_t *dict)
{
    free(dict);
}

static inode_t *
test_inode_ref(inode_t *inode)
{
    return inode;
}

static inode_t *
test_inode_unref(inode_t *inode)
{
    return inode;
}

static int
test_setxattr(xlator_t *subvol, loc_t *loc, dict_t *dict, int32_t flags,
              dict_t *xdata, dict_t **result)
{
    int call;
    int ret = 0;

    pthread_mutex_lock(&backend_lock);
    call = ++entered;
    pthread_cond_broadcast(&backend_cond);
    while ((call == 1 && !release_first) || (call == 2 && !release_second))
        pthread_cond_wait(&backend_cond, &backend_lock);
    if (fail_backend)
        ret = -EIO;
    else
        persisted = ((struct test_dict *)dict)->value;
    pthread_mutex_unlock(&backend_lock);
    return ret;
}

static void *
flush(void *unused)
{
    sync_data_to_disk(&translator, &quota);
    return NULL;
}

static void
concurrent_flush(int64_t base, int64_t first_delta, int64_t second_delta)
{
    pthread_t first;
    pthread_t second;
    struct timespec deadline;

    entered = release_first = release_second = 0;
    quota.xattr_size = quota.total_size = persisted = base;
    GF_ATOMIC_INIT(quota.pending_update, first_delta);
    assert(pthread_create(&first, NULL, flush, NULL) == 0);

    pthread_mutex_lock(&backend_lock);
    while (entered < 1)
        pthread_cond_wait(&backend_cond, &backend_lock);
    GF_ATOMIC_ADD(quota.pending_update, second_delta);
    assert(pthread_create(&second, NULL, flush, NULL) == 0);

    /* Unpatched code lets the second flush reach the backend using the old
     * base. Patched code holds it at the namespace lock until the first exits.
     */
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec++;
    while (entered < 2) {
        if (pthread_cond_timedwait(&backend_cond, &backend_lock, &deadline) ==
            ETIMEDOUT)
            break;
    }
    release_first = 1;
    pthread_cond_broadcast(&backend_cond);
    pthread_mutex_unlock(&backend_lock);
    pthread_join(first, NULL);

    pthread_mutex_lock(&backend_lock);
    release_second = 1;
    pthread_cond_broadcast(&backend_cond);
    pthread_mutex_unlock(&backend_lock);
    pthread_join(second, NULL);

    assert(persisted == base + first_delta + second_delta);
    assert(quota.xattr_size == persisted);
    assert(GF_ATOMIC_GET(quota.pending_update) == 0);
}

int
main(void)
{
    assert(synclock_init(&quota.sync_lock, SYNC_LOCK_DEFAULT) == 0);
    LOCK_INIT(&quota.lock);
    quota.ns = &namespace;
   private
    .no_distribute = true;
    translator.private = &private;
    translator.children = &child;

    concurrent_flush(0, 100, 200);
    concurrent_flush(300, -100, -200);

    /* Failed persistence must retain the adjustment for the next flush. */
    entered = 0;
    release_first = release_second = 1;
    fail_backend = 1;
    GF_ATOMIC_INIT(quota.pending_update, 100);
    sync_data_to_disk(&translator, &quota);
    assert(quota.xattr_size == 0);
    assert(GF_ATOMIC_GET(quota.pending_update) == 100);
    fail_backend = 0;
    sync_data_to_disk(&translator, &quota);
    assert(persisted == 100);
    assert(GF_ATOMIC_GET(quota.pending_update) == 0);

    LOCK_DESTROY(&quota.lock);
    synclock_destroy(&quota.sync_lock);
    return 0;
}
