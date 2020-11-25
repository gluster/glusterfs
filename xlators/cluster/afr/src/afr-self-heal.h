/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _AFR_SELFHEAL_H
#define _AFR_SELFHEAL_H

/* Perform fop on all UP subvolumes and wait for all callbacks to return */

#define AFR_ONALL(frame, rfn, fop, args...)                                    \
    do {                                                                       \
        afr_local_t *__local = frame->local;                                   \
        afr_private_t *__priv = frame->this->private;                          \
        int __i = 0, __count = 0;                                              \
        unsigned char *__child_up = alloca(__priv->child_count);               \
                                                                               \
        memcpy(__child_up, __priv->child_up,                                   \
               sizeof(*__child_up) * __priv->child_count);                     \
        __count = AFR_COUNT(__child_up, __priv->child_count);                  \
                                                                               \
        __local->barrier.waitfor = __count;                                    \
        afr_local_replies_wipe(__local, __priv);                               \
                                                                               \
        for (__i = 0; __i < __priv->child_count; __i++) {                      \
            if (!__child_up[__i])                                              \
                continue;                                                      \
            STACK_WIND_COOKIE(frame, rfn, (void *)(long)__i,                   \
                              __priv->children[__i],                           \
                              __priv->children[__i]->fops->fop, args);         \
        }                                                                      \
        syncbarrier_wait(&__local->barrier, __count);                          \
    } while (0)

/* Perform fop on all subvolumes represented by list[] array and wait
   for all callbacks to return */

#define AFR_ONLIST(list, frame, rfn, fop, args...)                             \
    do {                                                                       \
        afr_local_t *__local = frame->local;                                   \
        afr_private_t *__priv = frame->this->private;                          \
        int __i = 0;                                                           \
        int __count = 0;                                                       \
        unsigned char *__list = alloca(__priv->child_count);                   \
                                                                               \
        memcpy(__list, list, sizeof(*__list) * __priv->child_count);           \
        __count = AFR_COUNT(__list, __priv->child_count);                      \
        __local->barrier.waitfor = __count;                                    \
        afr_local_replies_wipe(__local, __priv);                               \
                                                                               \
        for (__i = 0; __i < __priv->child_count; __i++) {                      \
            if (!__list[__i])                                                  \
                continue;                                                      \
            STACK_WIND_COOKIE(frame, rfn, (void *)(long)__i,                   \
                              __priv->children[__i],                           \
                              __priv->children[__i]->fops->fop, args);         \
        }                                                                      \
        syncbarrier_wait(&__local->barrier, __count);                          \
    } while (0)

#define AFR_SEQ(frame, rfn, fop, args...)                                      \
    do {                                                                       \
        afr_local_t *__local = frame->local;                                   \
        afr_private_t *__priv = frame->this->private;                          \
        int __i = 0;                                                           \
                                                                               \
        afr_local_replies_wipe(__local, __priv);                               \
                                                                               \
        for (__i = 0; __i < __priv->child_count; __i++) {                      \
            if (!__priv->child_up[__i])                                        \
                continue;                                                      \
            STACK_WIND_COOKIE(frame, rfn, (void *)(long)__i,                   \
                              __priv->children[__i],                           \
                              __priv->children[__i]->fops->fop, args);         \
            syncbarrier_wait(&__local->barrier, 1);                            \
        }                                                                      \
    } while (0)

#define ALLOC_MATRIX(n, type)                                                  \
    ({                                                                         \
        int __i;                                                               \
        type **__ptr = alloca(n * sizeof(type *));                             \
                                                                               \
        for (__i = 0; __i < n; __i++)                                          \
            __ptr[__i] = alloca0(n * sizeof(type));                            \
        __ptr;                                                                 \
    })

#define IA_EQUAL(f, s, field)                                                  \
    (memcmp(&(f.ia_##field), &(s.ia_##field), sizeof(s.ia_##field)) == 0)

#define SBRAIN_HEAL_NO_GO_MSG                                                  \
    "Failed to obtain replies from all bricks of "                             \
    "the replica (are they up?). Cannot resolve split-brain."
#define SFILE_NOT_IN_SPLIT_BRAIN "File not in split-brain"
#define SNO_BIGGER_FILE "No bigger file"
#define SNO_DIFF_IN_MTIME "No difference in mtime"
#define SUSE_SOURCE_BRICK_TO_HEAL                                              \
    "Use source-brick option to heal metadata"                                 \
    " split-brain"
#define SINVALID_BRICK_NAME "Invalid brick name"
#define SBRICK_IS_NOT_UP "Brick is not up"
#define SBRICK_NOT_CONNECTED "Brick is not connected"
#define SLESS_THAN2_BRICKS_in_REP "< 2 bricks in replica are up"
#define SBRICK_IS_REMOTE "Brick is remote"
#define SSTARTED_SELF_HEAL "Started self-heal"
#define SOP_NOT_SUPPORTED "Operation Not Supported"
#define SFILE_NOT_UNDER_DATA                                                   \
    "The file is not under data or metadata "                                  \
    "split-brain"
#define SFILE_NOT_IN_SPLIT_BRAIN "File not in split-brain"
#define SALL_BRICKS_UP_TO_RESOLVE                                              \
    "All the bricks should be up to resolve the"                               \
    " gfid split brain"
#define SERROR_GETTING_SRC_BRICK "Error getting the source brick"
int
afr_selfheal(xlator_t *this, uuid_t gfid);

gf_boolean_t
afr_throttled_selfheal(call_frame_t *frame, xlator_t *this);

int
afr_selfheal_name(xlator_t *this, uuid_t gfid, const char *name, void *gfid_req,
                  dict_t *req, dict_t *rsp);

int
afr_selfheal_data(call_frame_t *frame, xlator_t *this, fd_t *fd);

int
afr_selfheal_metadata(call_frame_t *frame, xlator_t *this, inode_t *inode);

int
afr_selfheal_entry(call_frame_t *frame, xlator_t *this, inode_t *inode);

int
afr_lookup_and_heal_gfid(xlator_t *this, inode_t *parent, const char *name,
                         inode_t *inode, struct afr_reply *replies, int source,
                         unsigned char *sources, void *gfid, int *gfid_idx);

int
afr_selfheal_inodelk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                     char *dom, off_t off, size_t size,
                     unsigned char *locked_on);

int
afr_selfheal_tryinodelk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                        char *dom, off_t off, size_t size,
                        unsigned char *locked_on);

int
afr_selfheal_tie_breaker_inodelk(call_frame_t *frame, xlator_t *this,
                                 inode_t *inode, char *dom, off_t off,
                                 size_t size, unsigned char *locked_on);

int
afr_selfheal_uninodelk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                       char *dom, off_t off, size_t size,
                       const unsigned char *locked_on);

int
afr_selfheal_entrylk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                     char *dom, const char *name, unsigned char *locked_on);

int
afr_selfheal_tryentrylk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                        char *dom, const char *name, unsigned char *locked_on);

int
afr_selfheal_tie_breaker_entrylk(call_frame_t *frame, xlator_t *this,
                                 inode_t *inode, char *dom, const char *name,
                                 unsigned char *locked_on);

int
afr_selfheal_unentrylk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                       char *dom, const char *name, unsigned char *locked_on,
                       dict_t *xdata);

int
afr_selfheal_unlocked_discover(call_frame_t *frame, inode_t *inode, uuid_t gfid,
                               struct afr_reply *replies);

int
afr_selfheal_unlocked_discover_on(call_frame_t *frame, inode_t *inode,
                                  uuid_t gfid, struct afr_reply *replies,
                                  unsigned char *discover_on, dict_t *dict);
inode_t *
afr_selfheal_unlocked_lookup_on(call_frame_t *frame, inode_t *parent,
                                const char *name, struct afr_reply *replies,
                                unsigned char *lookup_on, dict_t *xattr);

int
afr_selfheal_find_direction(call_frame_t *frame, xlator_t *this,
                            struct afr_reply *replies,
                            afr_transaction_type type, unsigned char *locked_on,
                            unsigned char *sources, unsigned char *sinks,
                            uint64_t *witness, unsigned char *flag);
int
afr_selfheal_fill_matrix(xlator_t *this, int **matrix, int subvol, int idx,
                         dict_t *xdata);

int
afr_selfheal_extract_xattr(xlator_t *this, struct afr_reply *replies,
                           afr_transaction_type type, int *dirty, int **matrix);

int
afr_sh_generic_fop_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, struct iatt *pre,
                       struct iatt *post, dict_t *xdata);

int
afr_selfheal_restore_time(call_frame_t *frame, xlator_t *this, inode_t *inode,
                          int source, unsigned char *healed_sinks,
                          struct afr_reply *replies);
int
afr_selfheal_undo_pending(call_frame_t *frame, xlator_t *this, inode_t *inode,
                          unsigned char *sources, unsigned char *sinks,
                          unsigned char *healed_sinks,
                          unsigned char *undid_pending,
                          afr_transaction_type type, struct afr_reply *replies,
                          unsigned char *locked_on);

int
afr_selfheal_recreate_entry(call_frame_t *frame, int dst, int source,
                            unsigned char *sources, inode_t *dir,
                            const char *name, inode_t *inode,
                            struct afr_reply *replies);

int
afr_selfheal_post_op(call_frame_t *frame, xlator_t *this, inode_t *inode,
                     int subvol, dict_t *xattr, dict_t *xdata);

call_frame_t *
afr_frame_create(xlator_t *this, int32_t *op_errno);

inode_t *
afr_inode_find(xlator_t *this, uuid_t gfid);

int
afr_selfheal_discover_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int op_ret, int op_errno, inode_t *inode,
                          struct iatt *buf, dict_t *xdata, struct iatt *parbuf);
void
afr_reply_copy(struct afr_reply *dst, struct afr_reply *src);

void
afr_replies_copy(struct afr_reply *dst, struct afr_reply *src, int count);

int
afr_selfheal_newentry_mark(call_frame_t *frame, xlator_t *this, inode_t *inode,
                           int source, struct afr_reply *replies,
                           unsigned char *sources, unsigned char *newentry);

unsigned int
afr_success_count(struct afr_reply *replies, unsigned int count);

void
afr_log_selfheal(uuid_t gfid, xlator_t *this, int ret, char *type, int source,
                 unsigned char *sources, unsigned char *healed_sinks);

void
afr_mark_largest_file_as_source(xlator_t *this, unsigned char *sources,
                                struct afr_reply *replies);
void
afr_mark_active_sinks(xlator_t *this, unsigned char *sources,
                      unsigned char *locked_on, unsigned char *sinks);

gf_boolean_t
afr_dict_contains_heal_op(call_frame_t *frame);

gf_boolean_t
afr_can_decide_split_brain_source_sinks(struct afr_reply *replies,
                                        int child_count);
int
afr_mark_split_brain_source_sinks(
    call_frame_t *frame, xlator_t *this, inode_t *inode, unsigned char *sources,
    unsigned char *sinks, unsigned char *healed_sinks, unsigned char *locked_on,
    struct afr_reply *replies, afr_transaction_type type);

int
afr_sh_get_fav_by_policy(xlator_t *this, struct afr_reply *replies,
                         inode_t *inode, char **policy_str);

int
_afr_fav_child_reset_sink_xattrs(call_frame_t *frame, xlator_t *this,
                                 inode_t *inode, int source,
                                 unsigned char *healed_sinks,
                                 unsigned char *undid_pending,
                                 afr_transaction_type type,
                                 unsigned char *locked_on,
                                 struct afr_reply *replies);

int
afr_get_child_index_from_name(xlator_t *this, char *name);

gf_boolean_t
afr_does_witness_exist(xlator_t *this, uint64_t *witness);

int
__afr_selfheal_data_prepare(call_frame_t *frame, xlator_t *this, inode_t *inode,
                            unsigned char *locked_on, unsigned char *sources,
                            unsigned char *sinks, unsigned char *healed_sinks,
                            unsigned char *undid_pending,
                            struct afr_reply *replies, unsigned char *flag);

int
__afr_selfheal_metadata_prepare(call_frame_t *frame, xlator_t *this,
                                inode_t *inode, unsigned char *locked_on,
                                unsigned char *sources, unsigned char *sinks,
                                unsigned char *healed_sinks,
                                unsigned char *undid_pending,
                                struct afr_reply *replies, unsigned char *flag);
int
__afr_selfheal_entry_prepare(call_frame_t *frame, xlator_t *this,
                             inode_t *inode, unsigned char *locked_on,
                             unsigned char *sources, unsigned char *sinks,
                             unsigned char *healed_sinks,
                             struct afr_reply *replies, int *source_p,
                             unsigned char *flag);

int
afr_selfheal_unlocked_inspect(call_frame_t *frame, xlator_t *this, uuid_t gfid,
                              inode_t **link_inode, gf_boolean_t *data_selfheal,
                              gf_boolean_t *metadata_selfheal,
                              gf_boolean_t *entry_selfheal,
                              struct afr_reply *replies);

int
afr_selfheal_do(call_frame_t *frame, xlator_t *this, uuid_t gfid);

int
afr_selfheal_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xdata);

int
afr_locked_fill(call_frame_t *frame, xlator_t *this, unsigned char *locked_on);
int
afr_choose_source_by_policy(afr_private_t *priv, unsigned char *sources,
                            afr_transaction_type type);

int
afr_selfheal_metadata_by_stbuf(xlator_t *this, struct iatt *stbuf);

int
afr_sh_fav_by_size(xlator_t *this, struct afr_reply *replies, inode_t *inode);
int
afr_sh_fav_by_mtime(xlator_t *this, struct afr_reply *replies, inode_t *inode);
int
afr_sh_fav_by_ctime(xlator_t *this, struct afr_reply *replies, inode_t *inode);

int
afr_gfid_split_brain_source(xlator_t *this, struct afr_reply *replies,
                            inode_t *inode, uuid_t pargfid, const char *bname,
                            int src_idx, int child_idx,
                            unsigned char *locked_on, int *src, dict_t *req,
                            dict_t *rsp);
int
afr_mark_source_sinks_if_file_empty(xlator_t *this, unsigned char *sources,
                                    unsigned char *sinks,
                                    unsigned char *healed_sinks,
                                    unsigned char *locked_on,
                                    struct afr_reply *replies,
                                    afr_transaction_type type);

gf_boolean_t
afr_is_file_empty_on_all_children(afr_private_t *priv,
                                  struct afr_reply *replies);

int
afr_selfheal_entry_delete(xlator_t *this, inode_t *dir, const char *name,
                          inode_t *inode, int child, struct afr_reply *replies);
int
afr_anon_inode_create(xlator_t *this, int child, inode_t **linked_inode);
#endif /* !_AFR_SELFHEAL_H */
