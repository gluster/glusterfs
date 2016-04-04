/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <sys/stat.h>
#include <sys/types.h>

#define LEADER_XATTR            "user.jbr.leader"
#define SECOND_CHILD(xl)        (xl->children->next->xlator)
#define RECONCILER_PATH         JBR_SCRIPT_PREFIX"/reconciler.py"
#define CHANGELOG_ENTRY_SIZE    128

enum {
        gf_mt_jbr_private_t = gf_common_mt_end + 1,
        gf_mt_jbr_fd_ctx_t,
        gf_mt_jbr_inode_ctx_t,
        gf_mt_jbr_dirty_t,
        gf_mt_jbr_end
};

typedef enum jbr_recon_notify_ev_id_t {
        JBR_RECON_SET_LEADER = 1,
        JBR_RECON_ADD_CHILD = 2
} jbr_recon_notify_ev_id_t;

typedef struct _jbr_recon_notify_ev_s {
        jbr_recon_notify_ev_id_t id;
        uint32_t index; /* in case of add */
        struct list_head list;
} jbr_recon_notify_ev_t;

typedef struct {
        /*
         * This is a hack to allow a non-leader to accept requests while the
         * leader is down, and it only works for n=2.  The way it works is that
         * "config_leader" indicates the state from our options (via init or
         * reconfigure) but "leader" is what the fop code actually looks at.  If
         * config_leader is true, then leader will *always* be true as well,
         * giving that brick precedence.  If config_leader is false, then
         * leader will only be true if there is no connection to the other
         * brick (tracked in jbr_notify).
         *
         * TBD: implement real leader election
         */
        gf_boolean_t            config_leader;
        gf_boolean_t            leader;
        uint8_t                 up_children;
        uint8_t                 n_children;
        char                    *vol_file;
        uint32_t                current_term;
        uint32_t                kid_state;
        gf_lock_t               dirty_lock;
        struct list_head        dirty_fds;
	uint32_t                index;
	gf_lock_t               index_lock;
        double                  quorum_pct;
        int                     term_fd;
        long                    term_total;
        long                    term_read;
        /*
         * This is a super-duper hack, but it will do for now.  The reason it's
         * a hack is that we pass this to dict_set_static_bin, so we don't have
         * to mess around with allocating and freeing it on every single IPC
         * request, but it's totally not thread-safe.  On the other hand, there
         * should only be one reconciliation thread running and calling these
         * functions at a time, so maybe that doesn't matter.
         *
         * TBD: re-evaluate how to manage this
         */
        char                    term_buf[CHANGELOG_ENTRY_SIZE];
        gf_boolean_t            child_up; /* To maintain the state of *
                                           * the translator */
} jbr_private_t;

typedef struct {
        call_stub_t             *stub;
        call_stub_t             *qstub;
        uint32_t                call_count;
        uint32_t                successful_acks;
        uint32_t                successful_op_ret;
        fd_t                    *fd;
        struct list_head        qlinks;
} jbr_local_t;

/*
 * This should match whatever changelog returns on the pre-op for us to pass
 * when we're ready for our post-op.
 */
typedef uint32_t log_id_t;

typedef struct {
        struct list_head        links;
        log_id_t                id;
} jbr_dirty_list_t;

typedef struct {
        fd_t                    *fd;
        struct list_head        dirty_list;
        struct list_head        fd_list;
} jbr_fd_ctx_t;

typedef struct {
        gf_lock_t               lock;
        uint32_t                active;
        struct list_head        aqueue;
        uint32_t                pending;
        struct list_head        pqueue;
} jbr_inode_ctx_t;

void jbr_start_reconciler (xlator_t *this);
