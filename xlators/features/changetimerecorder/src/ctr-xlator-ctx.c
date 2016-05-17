/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "ctr-xlator-ctx.h"
#include "ctr-messages.h"
#include <time.h>
#include <sys/time.h>

#define IS_THE_ONLY_HARDLINK(ctr_hard_link)\
        (ctr_hard_link->list.next == ctr_hard_link->list.prev)


static void
fini_ctr_hard_link (ctr_hard_link_t **ctr_hard_link) {

        GF_ASSERT (ctr_hard_link);

        if (*ctr_hard_link)
                return;
        GF_FREE ((*ctr_hard_link)->base_name);
        GF_FREE (*ctr_hard_link);
        *ctr_hard_link = NULL;
}


/* Please lock the ctr_xlator_ctx before using this function */
ctr_hard_link_t *
ctr_search_hard_link_ctx (xlator_t                  *this,
                            ctr_xlator_ctx_t        *ctr_xlator_ctx,
                            uuid_t                  pgfid,
                            const char              *base_name)
{
        ctr_hard_link_t *_hard_link             = NULL;
        ctr_hard_link_t *searched_hardlink      = NULL;

        GF_ASSERT (this);
        GF_ASSERT (ctr_xlator_ctx);

        if (pgfid == NULL || base_name == NULL)
                goto out;

        /*linear search*/
        list_for_each_entry (_hard_link, &ctr_xlator_ctx->hardlink_list, list) {
                if (gf_uuid_compare (_hard_link->pgfid, pgfid) == 0
                        && _hard_link->base_name
                        && strcmp(_hard_link->base_name, base_name) == 0) {
                        searched_hardlink = _hard_link;
                        break;
                }
        }

out:
        return searched_hardlink;
}




/* Please lock the ctr_xlator_ctx before using this function */
int
ctr_add_hard_link (xlator_t             *this,
               ctr_xlator_ctx_t         *ctr_xlator_ctx,
               uuid_t                   pgfid,
               const char               *base_name)
{
        int ret                                 = -1;
        ctr_hard_link_t *ctr_hard_link          = NULL;
        struct timeval  current_time                   = {0};

        GF_ASSERT (this);
        GF_ASSERT (ctr_xlator_ctx);

        if (pgfid == NULL || base_name == NULL)
                goto out;

        ctr_hard_link = GF_CALLOC (1, sizeof (*ctr_hard_link),
                                        gf_ctr_mt_hard_link_t);
        if (!ctr_hard_link) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CTR_MSG_CALLOC_FAILED, "Failed allocating "
                        "ctr_hard_link");
                goto out;
        }

        /*Initialize the ctr_hard_link object and
         * Assign the values : parent GFID and basename*/
        INIT_LIST_HEAD (&ctr_hard_link->list);
        gf_uuid_copy (ctr_hard_link->pgfid, pgfid);
        ret = gf_asprintf(&ctr_hard_link->base_name, "%s", base_name);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_COPY_FAILED, "Failed copying basename"
                        "to ctr_hard_link");
                goto error;
        }

        ret = gettimeofday (&current_time, NULL);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get current time");
                goto error;
        }

        /*Add the hard link to the list*/
        list_add_tail (&ctr_hard_link->list,
                        &ctr_xlator_ctx->hardlink_list);

        ctr_hard_link->hardlink_heal_period = current_time.tv_sec;

        /*aal izz well!*/
        ret = 0;
        goto out;
error:
        GF_FREE (ctr_hard_link);
out:
        return ret;
}

static void
__delete_hard_link_from_list (ctr_hard_link_t  **ctr_hard_link)
{
        GF_ASSERT (ctr_hard_link);
        GF_ASSERT (*ctr_hard_link);

        /*Remove hard link from list*/
        list_del(&(*ctr_hard_link)->list);
        fini_ctr_hard_link (ctr_hard_link);
}


int
ctr_delete_hard_link (xlator_t                *this,
                  ctr_xlator_ctx_t      *ctr_xlator_ctx,
                  uuid_t                pgfid,
                  const char            *base_name)
{
        int ret                         = -1;
        ctr_hard_link_t *ctr_hard_link     = NULL;

        GF_ASSERT (this);
        GF_ASSERT (ctr_xlator_ctx);


        LOCK (&ctr_xlator_ctx->lock);

        /*Check if the hard link is present */
        ctr_hard_link = ctr_search_hard_link_ctx (this, ctr_xlator_ctx,
                                                        pgfid, base_name);
        if (!ctr_hard_link) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_HARDLINK_MISSING_IN_LIST,
                        "Hard link doesn't exist in the list");
                goto out;
        }

        __delete_hard_link_from_list (&ctr_hard_link);
        ctr_hard_link = NULL;

        ret = 0;
out:
        UNLOCK (&ctr_xlator_ctx->lock);

        return ret;
}




int
ctr_update_hard_link (xlator_t                *this,
                  ctr_xlator_ctx_t      *ctr_xlator_ctx,
                  uuid_t                pgfid,
                  const char            *base_name,
                  uuid_t                old_pgfid,
                  const char            *old_base_name)
{
        int ret                            = -1;
        ctr_hard_link_t *ctr_hard_link     = NULL;
        struct timeval current_time        = {0};

        GF_ASSERT (this);
        GF_ASSERT (ctr_xlator_ctx);


        LOCK (&ctr_xlator_ctx->lock);

        /*Check if the hard link is present */
        ctr_hard_link = ctr_search_hard_link_ctx (this, ctr_xlator_ctx,
                                                old_pgfid, old_base_name);
        if (!ctr_hard_link) {
                gf_msg_trace (this->name, 0, "Hard link doesn't exist"
                              " in the list");
                /* Since the hard link is not present in the list
                 * we add it to the list */
                ret = ctr_add_hard_link (this, ctr_xlator_ctx,
                                        pgfid, base_name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_ADD_HARDLINK_TO_LIST_FAILED,
                                "Failed adding hard link to the list");
                        goto out;
                }
                ret = 0;
                goto out;
        }

        /* update the hard link */
        gf_uuid_copy (ctr_hard_link->pgfid, pgfid);
        GF_FREE (ctr_hard_link->base_name);
        ret = gf_asprintf(&ctr_hard_link->base_name, "%s", base_name);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_COPY_FAILED, "Failed copying basename"
                                        "to ctr_hard_link");
                /* delete the corrupted entry */
                __delete_hard_link_from_list (&ctr_hard_link);
                ctr_hard_link = NULL;
                goto out;
        }

        ret = gettimeofday (&current_time, NULL);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get current time");
                ctr_hard_link->hardlink_heal_period = 0;
        } else {
                ctr_hard_link->hardlink_heal_period = current_time.tv_sec;
        }

        ret = 0;

out:
        UNLOCK (&ctr_xlator_ctx->lock);

        return ret;
}




/* Delete all hardlinks */
static int
ctr_delete_all_hard_link (xlator_t *this,
                          ctr_xlator_ctx_t *ctr_xlator_ctx)
{
        int ret = -1;
        ctr_hard_link_t *ctr_hard_link     = NULL;
        ctr_hard_link_t *tmp               = NULL;

        GF_ASSERT (ctr_xlator_ctx);

        LOCK (&ctr_xlator_ctx->lock);

        list_for_each_entry_safe(ctr_hard_link, tmp,
                        &ctr_xlator_ctx->hardlink_list, list)
        {
                /*Remove hard link from list*/
                __delete_hard_link_from_list (&ctr_hard_link);
                ctr_hard_link = NULL;

        }


        UNLOCK (&ctr_xlator_ctx->lock);

        ret = 0;

        return ret;
}


/* Please lock the inode before using this function */
static ctr_xlator_ctx_t *
__get_ctr_xlator_ctx (xlator_t  *this,
                      inode_t   *inode)
{
        int ret                                 = 0;
        uint64_t _addr                          = 0;
        ctr_xlator_ctx_t *ctr_xlator_ctx        = NULL;

        GF_ASSERT (this);
        GF_ASSERT (inode);

        ret = __inode_ctx_get (inode, this, &_addr);
        if (ret < 0)
                _addr = 0;
        if (_addr != 0) {
                ctr_xlator_ctx = (ctr_xlator_ctx_t *) (long)_addr;
        }

        return ctr_xlator_ctx;
}


ctr_xlator_ctx_t *
init_ctr_xlator_ctx (xlator_t *this,
                     inode_t *inode)
{
        int ret                                 = -1;
        uint64_t _addr                          = 0;
        ctr_xlator_ctx_t *ctr_xlator_ctx        = NULL;
        struct timeval current_time             = {0};

        GF_ASSERT (this);
        GF_ASSERT (inode);

        LOCK (&inode->lock);
        {
                ctr_xlator_ctx = __get_ctr_xlator_ctx (this, inode);
                if (ctr_xlator_ctx) {
                        ret = 0;
                        goto out;
                }
                ctr_xlator_ctx = GF_CALLOC (1, sizeof (*ctr_xlator_ctx),
                                                        gf_ctr_mt_xlator_ctx);
                if (!ctr_xlator_ctx)
                        goto out;

                ret = LOCK_INIT (&ctr_xlator_ctx->lock);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ret,
                                CTR_MSG_INIT_LOCK_FAILED,
                                "Failed init lock %s", strerror(ret));
                        goto out;
                }
                _addr = (uint64_t) ctr_xlator_ctx;

                ret = __inode_ctx_set (inode, this, &_addr);
                if (ret) {
                        goto out;
                }

                INIT_LIST_HEAD (&ctr_xlator_ctx->hardlink_list);

                ret = gettimeofday (&current_time, NULL);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get current time");
                        goto out;
                }

                ctr_xlator_ctx->inode_heal_period = current_time.tv_sec;
        }
        ret = 0;
out:
        if (ret) {
                GF_FREE (ctr_xlator_ctx);
                ctr_xlator_ctx = NULL;
        }

        UNLOCK (&inode->lock);

        return ctr_xlator_ctx;
}




void
fini_ctr_xlator_ctx (xlator_t *this,
                     inode_t *inode)
{
        int ret                                 = 0;
        uint64_t _addr                          = 0;
        ctr_xlator_ctx_t *ctr_xlator_ctx        = NULL;


        inode_ctx_del (inode, this, &_addr);
        if (!_addr)
                return;

        ctr_xlator_ctx = (ctr_xlator_ctx_t *) (long) _addr;

        ret = ctr_delete_all_hard_link (this, ctr_xlator_ctx);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING , 0,
                        CTR_MSG_DELETE_HARDLINK_FAILED, "Failed deleting all "
                        "hard links from inode context");
        }

        LOCK_DESTROY (&ctr_xlator_ctx->lock);

        GF_FREE (ctr_xlator_ctx);

}




ctr_xlator_ctx_t *
get_ctr_xlator_ctx (xlator_t *this,
                    inode_t *inode)
{
        ctr_xlator_ctx_t *ctr_xlator_ctx = NULL;

        LOCK (&inode->lock);
        ctr_xlator_ctx = __get_ctr_xlator_ctx (this, inode);
        UNLOCK (&inode->lock);

        return ctr_xlator_ctx;
}

