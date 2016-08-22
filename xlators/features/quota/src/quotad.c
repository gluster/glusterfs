/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "quota.h"
#include "quotad-aggregator.h"
#include "common-utils.h"

int
qd_notify (xlator_t *this, int32_t event, void *data, ...)
{
        switch (event) {
        case GF_EVENT_PARENT_UP:
                quotad_aggregator_init (this);
        }

        default_notify (this, event, data);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_quota_mt_end + 1);

        if (0 != ret) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting "
                        "init failed");
                return ret;
        }

        return ret;
}

int32_t
qd_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        quotad_aggregator_lookup_cbk_t  lookup_cbk = NULL;
        gfs3_lookup_rsp                 rsp = {0, };

        lookup_cbk = cookie;

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;

        gf_stat_from_iatt (&rsp.postparent, postparent);

        GF_PROTOCOL_DICT_SERIALIZE (this, xdata, (&rsp.xdata.xdata_val),
                                    rsp.xdata.xdata_len, rsp.op_errno, out);

        gf_stat_from_iatt (&rsp.stat, buf);

out:
        lookup_cbk (this, frame, &rsp);

        GF_FREE (rsp.xdata.xdata_val);

        inode_unref (inode);

        return 0;
}

xlator_t *
qd_find_subvol (xlator_t *this, char *volume_uuid)
{
        xlator_list_t *child  = NULL;
        xlator_t      *subvol = NULL;
        char           key[1024];
        char          *optstr = NULL;

        if (!this || !volume_uuid)
                goto out;

        for (child = this->children; child; child = child->next) {
                snprintf(key, 1024, "%s.volume-id", child->xlator->name);
                if (dict_get_str(this->options, key, &optstr) < 0)
                        continue;

                if (strcmp (optstr, volume_uuid) == 0) {
                        subvol = child->xlator;
                        break;
                }
        }

out:
        return subvol;
}

int
qd_nameless_lookup (xlator_t *this, call_frame_t *frame, gfs3_lookup_req *req,
                    dict_t *xdata, quotad_aggregator_lookup_cbk_t lookup_cbk)
{
        gfs3_lookup_rsp            rsp         = {0, };
        int                        op_errno    = 0, ret = -1;
        loc_t                      loc         = {0, };
        quotad_aggregator_state_t *state       = NULL;
        xlator_t                  *subvol      = NULL;
        char                      *volume_uuid = NULL;

        state = frame->root->state;

        frame->root->op = GF_FOP_LOOKUP;

        loc.inode = inode_new (state->itable);
        if (loc.inode == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        memcpy (loc.gfid, req->gfid, 16);

        ret = dict_get_str (xdata, "volume-uuid", &volume_uuid);
        if (ret < 0) {
                op_errno = EINVAL;
                goto out;
        }

        ret = dict_set_int8 (xdata, QUOTA_READ_ONLY_KEY, 1);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                        Q_MSG_ENOMEM, "dict set failed");
                ret = -ENOMEM;
                goto out;
        }

        subvol = qd_find_subvol (this, volume_uuid);
        if (subvol == NULL) {
                op_errno = EINVAL;
                goto out;
        }

        STACK_WIND_COOKIE (frame, qd_lookup_cbk, lookup_cbk, subvol,
                           subvol->fops->lookup, &loc, xdata);
        return 0;

out:
        rsp.op_ret = -1;
        rsp.op_errno = op_errno;

        lookup_cbk (this, frame, &rsp);

        inode_unref (loc.inode);
        return 0;
}

int
qd_reconfigure (xlator_t *this, dict_t *options)
{
        /* As of now quotad is restarted upon alteration of volfile */
        return 0;
}

void
qd_fini (xlator_t *this)
{
        quota_priv_t    *priv           = NULL;

        if (this == NULL || this->private == NULL)
                goto out;

        priv = this->private;

        if (priv->rpcsvc) {
                GF_FREE (priv->rpcsvc);
                priv->rpcsvc = NULL;
        }

        GF_FREE (priv);

out:
        return;
}

int32_t
qd_init (xlator_t *this)
{
        int32_t          ret            = -1;
        quota_priv_t    *priv           = NULL;

        if (NULL == this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: quota (%s) not configured for min of 1 child",
                        this->name);
                ret = -1;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (priv, quota_priv_t, err);
        LOCK_INIT (&priv->lock);

        this->private = priv;

        ret = 0;
err:
        if (ret) {
                GF_FREE (priv);
        }
        return ret;
}

class_methods_t class_methods = {
        .init           = qd_init,
        .fini           = qd_fini,
        .reconfigure    = qd_reconfigure,
        .notify         = qd_notify
};

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key   = {"transport-type"},
          .value = {"rpc", "rpc-over-rdma", "tcp", "socket", "ib-verbs",
                    "unix", "ib-sdp", "tcp/server", "ib-verbs/server", "rdma",
                    "rdma*([ \t]),*([ \t])socket",
                    "rdma*([ \t]),*([ \t])tcp",
                    "tcp*([ \t]),*([ \t])rdma",
                    "socket*([ \t]),*([ \t])rdma"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"transport.*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        {.key = {NULL}}
};
