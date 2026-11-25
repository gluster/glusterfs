#include <stdio.h>

#include <glusterfs/glusterfs.h>
#include <glusterfs/inode.h>
#include <glusterfs/xlator.h>

struct test_state {
    inode_t *target;
    unsigned int attempts;
};

static int32_t
invalidate(xlator_t *xl, inode_t *inode)
{
    struct test_state *state = xl->private;

    if (inode == state->target && ++state->attempts == 1) {
        /* Model do_forget() before prune records callback success. Prune
         * still owns its temporary ref. Forget only one of two lookups. */
        inode_forget_with_unref(inode_ref(inode), 1);
    }

    return 0;
}

int
main(void)
{
    struct test_state state = {0};
    xlator_t xl = {.name = "fuse-invalidate-forget-race", .private = &state};
    glusterfs_ctx_t *ctx = NULL;
    inode_table_t *table = NULL;
    inode_t *other = NULL;
    int ret = 1;

    ctx = glusterfs_ctx_new();
    if (!ctx || glusterfs_globals_init(ctx)) {
        fprintf(stderr, "Could not initialize GlusterFS context\n");
        return 1;
    }
    THIS->ctx = ctx;
    xl.ctx = ctx;

    table = inode_table_with_invalidator(0, &xl, invalidate, &xl, 0, 0);
    if (!table) {
        fprintf(stderr, "Could not create inode table\n");
        return 1;
    }

    state.target = inode_new(table);
    other = inode_new(table);
    if (!state.target || !other) {
        fprintf(stderr, "Could not create test inodes\n");
        inode_unref(state.target);
        inode_unref(other);
        goto out;
    }

    inode_lookup(state.target);
    inode_lookup(state.target);
    inode_lookup(other);
    inode_unref(state.target);
    inode_unref(other);

    inode_table_set_lru_limit(table, 1);
    if (state.attempts != 1) {
        fprintf(stderr, "Expected one initial invalidation, got %u\n",
                state.attempts);
        goto out;
    }

    /* Refresh the other inode so the target is the oldest LRU entry.
     * Dropping the ref puts us over the limit and triggers another prune. */
    inode_unref(inode_ref(other));
    if (state.attempts != 2) {
        fprintf(stderr,
                "Expected invalidation retry after FORGET, got %u "
                "attempt(s)\n",
                state.attempts);
        goto out;
    }

    ret = 0;
out:
    inode_table_destroy(table);
    return ret;
}
