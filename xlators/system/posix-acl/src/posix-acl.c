/*
  Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <errno.h>

#include "xlator.h"
#include "glusterfs.h"

#include "posix-acl.h"
#include "posix-acl-xattr.h"
#include "posix-acl-mem-types.h"
#include "posix-acl-messages.h"

#define UINT64(ptr) ((uint64_t)((long)(ptr)))
#define PTR(num) ((void *)((long)(num)))


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_posix_acl_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
                return ret;
        }

        return ret;
}

static uid_t
r00t ()
{
        struct posix_acl_conf *conf = NULL;

        conf = THIS->private;

        return conf->super_uid;
}


int
whitelisted_xattr (const char *key)
{
        if (!key)
                return 0;

        if (strcmp (POSIX_ACL_ACCESS_XATTR, key) == 0)
                return 1;
        if (strcmp (POSIX_ACL_DEFAULT_XATTR, key) == 0)
                return 1;
        return 0;
}


int
frame_is_user (call_frame_t *frame, uid_t uid)
{
        return (frame->root->uid == uid);
}


int
frame_is_super_user (call_frame_t *frame)
{
        int ret;

        ret = frame_is_user (frame, r00t());
        if (!ret)
                ret = frame_is_user (frame, 0);

        return ret;
}


int
frame_in_group (call_frame_t *frame, gid_t gid)
{
        int  i = 0;

        if (frame->root->gid == gid)
                return 1;

        for (i = 0; i < frame->root->ngrps; i++)
                if (frame->root->groups[i] == gid)
                        return 1;
        return 0;
}


mode_t
posix_acl_access_set_mode (struct posix_acl *acl, struct posix_acl_ctx *ctx)
{
        struct posix_ace  *ace = NULL;
        struct posix_ace  *group_ce = NULL;
        struct posix_ace  *mask_ce = NULL;
        int                count = 0;
        int                i = 0;
        mode_t             mode = 0;
        int                mask = 0;

        count = acl->count;

        ace = acl->entries;
        for (i = 0; i < count; i++) {
                switch (ace->tag) {
                case POSIX_ACL_USER_OBJ:
                        mask |= S_IRWXU;
                        mode |= (ace->perm << 6);
                        break;
                case POSIX_ACL_GROUP_OBJ:
                        group_ce = ace;
                        break;
                case POSIX_ACL_MASK:
                        mask_ce = ace;
                        break;
                case POSIX_ACL_OTHER:
                        mask |= S_IRWXO;
                        mode |= (ace->perm);
                        break;
                }
                ace++;
        }

        if (mask_ce) {
                mask |= S_IRWXG;
                mode |= (mask_ce->perm << 3);
        } else {
                if (!group_ce)
                        goto out;
                mask |= S_IRWXG;
                mode |= (group_ce->perm << 3);
        }

out:
        ctx->perm = (ctx->perm & ~mask) | mode;

        return mode;
}


static int
sticky_permits (call_frame_t *frame, inode_t *parent, inode_t *inode)
{
        struct posix_acl_ctx  *par = NULL;
        struct posix_acl_ctx  *ctx = NULL;

        if ((0 > frame->root->pid) || frame_is_super_user (frame))
                return 1;

        par = posix_acl_ctx_get (parent, frame->this);
        if (par == NULL)
                return 0;

        ctx = posix_acl_ctx_get (inode, frame->this);
        if (ctx == NULL)
                return 0;

        if (!(par->perm & S_ISVTX))
                return 1;

        if (frame_is_user (frame, par->uid))
                return 1;

        if (frame_is_user (frame, ctx->uid))
                return 1;

        return 0;
}

static gf_boolean_t
_does_acl_exist (struct posix_acl *acl)
{
        if (acl && (acl->count > POSIX_ACL_MINIMAL_ACE_COUNT))
                return _gf_true;
        return _gf_false;
}

static void
posix_acl_get_acl_string (call_frame_t *frame, struct posix_acl *acl,
                          char **acl_str)
{
        int                   i        = 0;
        size_t                size_acl = 0;
        size_t                offset   = 0;
        struct posix_ace      *ace     = NULL;
        char                  tmp_str[1024] = {0};
#define NON_GRP_FMT "(tag:%"PRIu16",perm:%"PRIu16",id:%"PRIu32")"
#define GRP_FMT "(tag:%"PRIu16",perm:%"PRIu16",id:%"PRIu32",in-groups:%d)"

        if (!_does_acl_exist (acl))
                goto out;

        ace = acl->entries;
        for (i = 0; i < acl->count; i++) {
                if (ace->tag != POSIX_ACL_GROUP) {
                        size_acl += snprintf (tmp_str, sizeof tmp_str,
                                     NON_GRP_FMT, ace->tag, ace->perm, ace->id);
                } else {
                        size_acl += snprintf (tmp_str, sizeof tmp_str,
                                        GRP_FMT, ace->tag,
                                       ace->perm, ace->id,
                                       frame_in_group (frame, ace->id));
                }

                ace++;
        }

        *acl_str = GF_CALLOC (1, size_acl + 1, gf_posix_acl_mt_char);
        if (!*acl_str)
                goto out;

        ace = acl->entries;
        for (i = 0; i < acl->count; i++) {
                if (ace->tag != POSIX_ACL_GROUP) {
                        offset += snprintf (*acl_str + offset,
                                            size_acl - offset,
                                     NON_GRP_FMT, ace->tag, ace->perm, ace->id);
                } else {
                        offset += snprintf (*acl_str + offset,
                                       size_acl - offset,
                                       GRP_FMT, ace->tag, ace->perm, ace->id,
                                       frame_in_group (frame, ace->id));
                }

                ace++;
        }
out:
        return;
}

static void
posix_acl_log_permit_denied (call_frame_t *frame, inode_t *inode, int want,
                             struct posix_acl_ctx *ctx, struct posix_acl *acl)
{
        char     *acl_str = NULL;
        client_t *client  = frame->root->client;

        if (!frame || !inode || !ctx)
                goto out;

        posix_acl_get_acl_string (frame, acl, &acl_str);

        gf_msg (frame->this->name, GF_LOG_INFO, EACCES, POSIX_ACL_MSG_EACCES,
                "client: %s, gfid: %s, req(uid:%d,gid:%d,perm:%d,"
                "ngrps:%"PRIu16"), ctx(uid:%d,gid:%d,in-groups:%d,perm:%d%d%d,"
                "updated-fop:%s, acl:%s)", client ? client->client_uid : "-",
                uuid_utoa (inode->gfid), frame->root->uid, frame->root->gid,
                want, frame->root->ngrps, ctx->uid, ctx->gid,
                frame_in_group (frame, ctx->gid), (ctx->perm & S_IRWXU) >> 6,
                (ctx->perm & S_IRWXG) >> 3, ctx->perm & S_IRWXO,
                gf_fop_string (ctx->fop), acl_str ? acl_str : "-");
out:
        GF_FREE (acl_str);
        return;
}

static int
acl_permits (call_frame_t *frame, inode_t *inode, int want)
{
        int                     verdict = 0;
        struct posix_acl       *acl = NULL;
        struct posix_ace       *ace = NULL;
        struct posix_acl_ctx   *ctx = NULL;
        struct posix_acl_conf  *conf = NULL;
        int                     i = 0;
        int                     perm = 0;
        int                     found = 0;
        int                     acl_present = 0;

        conf = frame->this->private;

        if ((0 > frame->root->pid) || frame_is_super_user (frame))
                goto green;

        ctx = posix_acl_ctx_get (inode, frame->this);
        if (!ctx)
                goto red;

        posix_acl_get (inode, frame->this, &acl, NULL);
        if (!acl) {
                acl = posix_acl_ref (frame->this, conf->minimal_acl);
        }

        ace = acl->entries;

        if (_does_acl_exist (acl))
                acl_present = 1;

        for (i = 0; i < acl->count; i++) {
                switch (ace->tag) {
                case POSIX_ACL_USER_OBJ:
                        perm = ((ctx->perm & S_IRWXU) >> 6);
                        if (frame_is_user (frame, ctx->uid))
                                goto perm_check;
                        break;
                case POSIX_ACL_USER:
                        perm = ace->perm;
                        if (frame_is_user (frame, ace->id))
                                goto mask_check;
                        break;
                case POSIX_ACL_GROUP_OBJ:
                        if (acl_present)
                                perm = ace->perm;
                        else
                                perm = ((ctx->perm & S_IRWXG) >> 3);
                        if (frame_in_group (frame, ctx->gid)) {
                                found = 1;
                                if ((perm & want) == want)
                                        goto mask_check;
                        }
                        break;
                case POSIX_ACL_GROUP:
                        perm = ace->perm;
                        if (frame_in_group (frame, ace->id)) {
                                found = 1;
                                if ((perm & want) == want)
                                        goto mask_check;
                        }
                        break;
                case POSIX_ACL_MASK:
                        break;
                case POSIX_ACL_OTHER:
                        perm = (ctx->perm & S_IRWXO);
                        if (!found)
                                goto perm_check;
                        /* fall through */
                default:
                        goto red;
                }

                ace++;
        }

mask_check:
        ace = acl->entries;

        for (i = 0; i < acl->count; i++, ace++) {
                if (ace->tag != POSIX_ACL_MASK)
                        continue;
                if ((ace->perm & perm & want) == want) {
                        goto green;
                }
                goto red;
        }

perm_check:
        if ((perm & want) == want) {
                goto green;
        } else {
                goto red;
        }

green:
        verdict = 1;
        goto out;
red:
        verdict = 0;
        posix_acl_log_permit_denied (frame, inode, want, ctx, acl);
out:
        if (acl)
                posix_acl_unref (frame->this, acl);

        return verdict;
}


struct posix_acl_ctx *
__posix_acl_ctx_get (inode_t *inode, xlator_t *this, gf_boolean_t create)
{
        struct posix_acl_ctx *ctx = NULL;
        uint64_t              int_ctx = 0;
        int                   ret = 0;

        ret = __inode_ctx_get (inode, this, &int_ctx);
        if ((ret == 0) && (int_ctx))
                return PTR(int_ctx);

        if (create == _gf_false)
                return NULL;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_posix_acl_mt_ctx_t);
        if (!ctx)
                return NULL;

        ret = __inode_ctx_put (inode, this, UINT64 (ctx));
        if (ret) {
                GF_FREE (ctx);
                ctx = NULL;
        }

        return ctx;
}

struct posix_acl_ctx *
posix_acl_ctx_new (inode_t *inode, xlator_t *this)
{
        struct posix_acl_ctx *ctx = NULL;

        if (inode == NULL) {
                gf_log_callingfn (this->name, GF_LOG_WARNING, "inode is NULL");
                return NULL;
        }

        LOCK (&inode->lock);
        {
                ctx = __posix_acl_ctx_get (inode, this, _gf_true);
        }
        UNLOCK (&inode->lock);

        if (ctx == NULL)
                gf_log_callingfn (this->name, GF_LOG_ERROR, "creating inode ctx"
                                  "failed for %s", uuid_utoa (inode->gfid));
        return ctx;
}

struct posix_acl_ctx *
posix_acl_ctx_get (inode_t *inode, xlator_t *this)
{
        struct posix_acl_ctx *ctx = NULL;

        if (inode == NULL) {
                gf_log_callingfn (this->name, GF_LOG_WARNING, "inode is NULL");
                return NULL;
        }

        LOCK (&inode->lock);
        {
                ctx = __posix_acl_ctx_get (inode, this, _gf_false);
        }
        UNLOCK (&inode->lock);

        if (ctx == NULL)
                gf_log_callingfn (this->name, GF_LOG_ERROR, "inode ctx is NULL "
                                  "for %s", uuid_utoa (inode->gfid));
        return ctx;
}

int
__posix_acl_set_specific (inode_t *inode, xlator_t *this,
                          gf_boolean_t is_access, struct posix_acl *acl)
{
        int                    ret = 0;
        struct posix_acl_ctx  *ctx = NULL;

        ctx = posix_acl_ctx_get (inode, this);
        if (!ctx) {
                ret = -1;
                goto out;
        }

        if (is_access)
                ctx->acl_access = acl;
        else
                ctx->acl_default = acl;
out:
        return ret;
}

int
__posix_acl_set (inode_t *inode, xlator_t *this, struct posix_acl *acl_access,
                 struct posix_acl *acl_default)
{
        int                    ret = 0;
        struct posix_acl_ctx  *ctx = NULL;

        ctx = posix_acl_ctx_get (inode, this);
        if (!ctx)
                goto out;

        ctx->acl_access = acl_access;
        ctx->acl_default = acl_default;

out:
        return ret;
}


int
__posix_acl_get (inode_t *inode, xlator_t *this, struct posix_acl **acl_access_p,
                 struct posix_acl **acl_default_p)
{
        int                    ret = 0;
        struct posix_acl_ctx  *ctx = NULL;

        ctx = posix_acl_ctx_get (inode, this);
        if (!ctx)
                goto out;

        if (acl_access_p)
                *acl_access_p = ctx->acl_access;
        if (acl_default_p)
                *acl_default_p = ctx->acl_default;

out:
        return ret;
}


struct posix_acl *
posix_acl_new (xlator_t *this, int entrycnt)
{
        struct posix_acl *acl = NULL;
        struct posix_ace *ace = NULL;

        acl = GF_CALLOC (1, sizeof (*acl) + (entrycnt * sizeof (*ace)),
                         gf_posix_acl_mt_posix_ace_t);
        if (!acl)
                return NULL;

        acl->count = entrycnt;

        posix_acl_ref (this, acl);

        return acl;
}


void
posix_acl_destroy (xlator_t *this, struct posix_acl *acl)
{
       GF_FREE (acl);

        return;
}


struct posix_acl *
posix_acl_ref (xlator_t *this, struct posix_acl *acl)
{
        struct posix_acl_conf  *conf = NULL;

        conf = this->private;

        LOCK(&conf->acl_lock);
        {
                acl->refcnt++;
        }
        UNLOCK(&conf->acl_lock);

        return acl;
}


struct posix_acl *
posix_acl_dup (xlator_t *this, struct posix_acl *acl)
{
        struct posix_acl       *dup = NULL;

        dup = posix_acl_new (this, acl->count);
        if (!dup)
                return NULL;

        memcpy (dup->entries, acl->entries,
                sizeof (struct posix_ace) * acl->count);

        return dup;
}


void
posix_acl_unref (xlator_t *this, struct posix_acl *acl)
{
        struct posix_acl_conf  *conf = NULL;
        int                     refcnt = 0;

        conf = this->private;

        LOCK(&conf->acl_lock);
        {
                refcnt = --acl->refcnt;
        }
        UNLOCK(&conf->acl_lock);

        if (!refcnt)
                posix_acl_destroy (this, acl);
}

int
posix_acl_set_specific (inode_t *inode, xlator_t *this, struct posix_acl *acl,
                        gf_boolean_t is_access)
{
        int                     ret = 0;
        int                     oldret = 0;
        struct posix_acl       *old_acl = NULL;
        struct posix_acl_conf  *conf = NULL;

        conf = this->private;

        LOCK (&conf->acl_lock);
        {
                if (is_access)
                        oldret = __posix_acl_get (inode, this, &old_acl, NULL);
                else
                        oldret = __posix_acl_get (inode, this, NULL, &old_acl);
                if (acl)
                        acl->refcnt++;
                ret = __posix_acl_set_specific (inode, this, is_access, acl);
        }
        UNLOCK (&conf->acl_lock);

        if (oldret == 0) {
                if (old_acl)
                        posix_acl_unref (this, old_acl);
        }

        return ret;
}

int
posix_acl_set (inode_t *inode, xlator_t *this, struct posix_acl *acl_access,
               struct posix_acl *acl_default)
{
        int                     ret = 0;
        int                     oldret = 0;
        struct posix_acl       *old_access = NULL;
        struct posix_acl       *old_default = NULL;
        struct posix_acl_conf  *conf = NULL;

        conf = this->private;

        LOCK(&conf->acl_lock);
        {
                oldret = __posix_acl_get (inode, this, &old_access,
                                          &old_default);
                if (acl_access)
                        acl_access->refcnt++;
                if (acl_default)
                        acl_default->refcnt++;

                ret = __posix_acl_set (inode, this, acl_access, acl_default);
        }
        UNLOCK(&conf->acl_lock);

        if (oldret == 0) {
                if (old_access)
                        posix_acl_unref (this, old_access);
                if (old_default)
                        posix_acl_unref (this, old_default);
        }

        return ret;
}


int
posix_acl_get (inode_t *inode, xlator_t *this, struct posix_acl **acl_access_p,
               struct posix_acl **acl_default_p)
{
        struct posix_acl_conf  *conf = NULL;
        struct posix_acl       *acl_access = NULL;
        struct posix_acl       *acl_default = NULL;
        int                     ret = 0;

        conf = this->private;

        LOCK(&conf->acl_lock);
        {
                ret = __posix_acl_get (inode, this, &acl_access, &acl_default);

                if (ret != 0)
                        goto unlock;

                if (acl_access && acl_access_p)
                        acl_access->refcnt++;
                if (acl_default && acl_default_p)
                        acl_default->refcnt++;
        }
unlock:
        UNLOCK(&conf->acl_lock);

        if (acl_access_p)
                *acl_access_p = acl_access;
        if (acl_default_p)
                *acl_default_p = acl_default;

        return ret;
}

mode_t
posix_acl_inherit_mode (struct posix_acl *acl, mode_t modein)
{
        struct posix_ace       *ace = NULL;
        int                     count = 0;
        int                     i = 0;
        mode_t                  newmode = 0;
        mode_t                  mode = 0;
        struct posix_ace       *mask_ce = NULL;
        struct posix_ace       *group_ce = NULL;

        newmode = mode = modein;

        count = acl->count;

        ace = acl->entries;
        for (i = 0; i < count; i++) {
                switch (ace->tag) {
                case POSIX_ACL_USER_OBJ:
                        ace->perm &= (mode >> 6) | ~S_IRWXO;
                        mode &= (ace->perm << 6) | ~S_IRWXU;
                        break;
                case POSIX_ACL_GROUP_OBJ:
                        group_ce = ace;
                        break;
                case POSIX_ACL_MASK:
                        mask_ce = ace;
                        break;
                case POSIX_ACL_OTHER:
                        ace->perm &= (mode) | ~S_IRWXO;
                        mode &= (ace->perm) | ~S_IRWXO;
                        break;
                }
                ace++;
        }

        if (mask_ce) {
                mask_ce->perm &= (mode >> 3) | ~S_IRWXO;
                mode &= (mask_ce->perm << 3) | ~S_IRWXG;
        } else if (group_ce) {
                group_ce->perm &= (mode >> 3) | ~S_IRWXO;
                mode &= (group_ce->perm << 3) | ~S_IRWXG;
        }

        newmode = ((modein & (S_IFMT | S_ISUID | S_ISGID | S_ISVTX)) |
		   (mode & (S_IRWXU|S_IRWXG|S_IRWXO)));

        return newmode;
}


mode_t
posix_acl_inherit (xlator_t *this, loc_t *loc, dict_t *params, mode_t mode,
                   int32_t umask, int is_dir)
{
        int                    ret = 0;
        struct posix_acl      *par_default = NULL;
        struct posix_acl      *acl_default = NULL;
        struct posix_acl      *acl_access = NULL;
        struct posix_acl_ctx  *ctx = NULL;
        char                  *xattr_default = NULL;
        char                  *xattr_access = NULL;
        int                    size_default = 0;
        int                    size_access = 0;
        mode_t                 retmode = 0;
        int16_t                tmp_mode = 0;
        mode_t                 client_umask = 0;

        retmode = mode;
        client_umask = umask;
        ret = dict_get_int16 (params, "umask", &tmp_mode);
        if (ret == 0) {
                client_umask = (mode_t)tmp_mode;
                dict_del (params, "umask");
                ret = dict_get_int16 (params, "mode", &tmp_mode);
                if (ret == 0) {
                        retmode = (mode_t)tmp_mode;
                        dict_del (params, "mode");
                } else {
                        gf_log (this->name, GF_LOG_ERROR,
                                "client sent umask, but not the original mode");
                }
        }

        ret = posix_acl_get (loc->parent, this, NULL, &par_default);

        if (!par_default)
                goto out;

        ctx = posix_acl_ctx_new (loc->inode, this);

        acl_access = posix_acl_dup (this, par_default);
        if (!acl_access)
                goto out;

        client_umask = 0; // No umask if we inherit an ACL
        retmode = posix_acl_inherit_mode (acl_access, retmode);
        ctx->perm = retmode;

        size_access = posix_acl_to_xattr (this, acl_access, NULL, 0);
        xattr_access = GF_CALLOC (1, size_access, gf_posix_acl_mt_char);
        if (!xattr_access) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = -1;
                goto out;
        }
        posix_acl_to_xattr (this, acl_access, xattr_access, size_access);

        ret = dict_set_bin (params, POSIX_ACL_ACCESS_XATTR, xattr_access,
                            size_access);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                GF_FREE (xattr_access);
                ret = -1;
                goto out;
        }

        if (!is_dir)
                goto set;


        acl_default = posix_acl_ref (this, par_default);

        size_default = posix_acl_to_xattr (this, acl_default, NULL, 0);
        xattr_default = GF_CALLOC (1, size_default, gf_posix_acl_mt_char);
        if (!xattr_default) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = -1;
                goto out;
        }
        posix_acl_to_xattr (this, acl_default, xattr_default, size_default);

        ret = dict_set_bin (params, POSIX_ACL_DEFAULT_XATTR, xattr_default,
                            size_default);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                GF_FREE (xattr_default);
                ret = -1;
                goto out;
        }

set:
        ret = posix_acl_set (loc->inode, this, acl_access, acl_default);
        if (ret != 0)
                goto out;

out:
        retmode &= ~client_umask;

        if (par_default)
                posix_acl_unref (this, par_default);
        if (acl_access)
                posix_acl_unref (this, acl_access);
        if (acl_default)
                posix_acl_unref (this, acl_default);

        return retmode;
}


mode_t
posix_acl_inherit_dir (xlator_t *this, loc_t *loc, dict_t *params, mode_t mode,
                       int32_t umask)
{
        mode_t  retmode = 0;

        retmode = posix_acl_inherit (this, loc, params, mode, umask, 1);

        return retmode;
}


mode_t
posix_acl_inherit_file (xlator_t *this, loc_t *loc, dict_t *params, mode_t mode,
                        int32_t umask)
{
        mode_t  retmode = 0;

        retmode = posix_acl_inherit (this, loc, params, mode, umask, 0);

        return retmode;
}

int
posix_acl_ctx_update (inode_t *inode, xlator_t *this, struct iatt *buf,
                      glusterfs_fop_t fop)
{
        struct posix_acl_ctx *ctx = NULL;
	struct posix_acl     *acl = NULL;
	struct posix_ace     *ace = NULL;
	struct posix_ace     *mask_ce = NULL;
	struct posix_ace     *group_ce = NULL;
        int                   ret = 0;
	int                   i = 0;

        LOCK(&inode->lock);
        {
                ctx = __posix_acl_ctx_get (inode, this, _gf_true);
                if (!ctx) {
                        ret = -1;
                        goto unlock;
                }

                ctx->uid   = buf->ia_uid;
                ctx->gid   = buf->ia_gid;
                ctx->perm  = st_mode_from_ia (buf->ia_prot, buf->ia_type);
                ctx->fop   = fop;

		acl = ctx->acl_access;
                if (!_does_acl_exist (acl))
			goto unlock;

		/* This is an extended ACL (not minimal acl). In case we
		   are only refreshing from iatt and not ACL xattrs (for
		   e.g. from postattributes of setattr() call, we need to
		   update the corresponding ACEs as well.
		*/
		ace = acl->entries;
		for (i = 0; i < acl->count; i++) {
			switch (ace->tag) {
			case POSIX_ACL_USER_OBJ:
				ace->perm = (ctx->perm & S_IRWXU) >> 6;
				break;
			case POSIX_ACL_USER:
			case POSIX_ACL_GROUP:
				break;
			case POSIX_ACL_GROUP_OBJ:
				group_ce = ace;
				break;
			case POSIX_ACL_MASK:
				mask_ce = ace;
				break;
			case POSIX_ACL_OTHER:
				ace->perm = (ctx->perm & S_IRWXO);
				break;
			}
			ace++;
		}

		if (mask_ce)
			mask_ce->perm = (ctx->perm & S_IRWXG) >> 3;
		else if (group_ce)
			group_ce->perm = (ctx->perm & S_IRWXG) >> 3;
		else
			ret = -1;
        }
unlock:
        UNLOCK(&inode->lock);
        return ret;
}

int
posix_acl_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, inode_t *inode,
                      struct iatt *buf, dict_t *xattr, struct iatt *postparent)
{
        struct posix_acl     *acl_access  = NULL;
        struct posix_acl     *acl_default = NULL;
        struct posix_acl     *old_access  = NULL;
        struct posix_acl     *old_default = NULL;
        struct posix_acl_ctx *ctx         = NULL;
        data_t               *data        = NULL;
        int                   ret         = 0;
        dict_t               *my_xattr    = NULL;

        if (op_ret != 0)
                goto unwind;

        ctx = posix_acl_ctx_new (inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        ret = posix_acl_get (inode, this, &old_access, &old_default);

        if (xattr == NULL)
                goto acl_set;

        data = dict_get (xattr, POSIX_ACL_ACCESS_XATTR);
        if (!data)
                goto acl_default;

        if (old_access &&
            posix_acl_matches_xattr (this, old_access, data->data,
                                     data->len)) {
                acl_access = posix_acl_ref (this, old_access);
        } else {
                acl_access = posix_acl_from_xattr (this, data->data,
                                                   data->len);
        }

acl_default:
        data = dict_get (xattr, POSIX_ACL_DEFAULT_XATTR);
        if (!data)
                goto acl_set;

        if (old_default &&
            posix_acl_matches_xattr (this, old_default, data->data,
                                     data->len)) {
                acl_default = posix_acl_ref (this, old_default);
        } else {
                acl_default = posix_acl_from_xattr (this, data->data,
                                                    data->len);
        }

acl_set:
        posix_acl_ctx_update (inode, this, buf, GF_FOP_LOOKUP);

        ret = posix_acl_set (inode, this, acl_access, acl_default);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set ACL in context");
unwind:
        my_xattr = frame->local;
        frame->local = NULL;
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, xattr,
                             postparent);

        if (acl_access)
                posix_acl_unref (this, acl_access);
        if (acl_default)
                posix_acl_unref (this, acl_default);
        if (old_access)
                posix_acl_unref (this, old_access);
        if (old_default)
                posix_acl_unref (this, old_default);
        if (my_xattr)
                dict_unref (my_xattr);

        return 0;
}


int
posix_acl_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *xattr)
{
        int      ret = 0;
        dict_t  *my_xattr = NULL;

        if (!loc->parent)
                /* lookup of / is always permitted */
                goto green;

        if (acl_permits (frame, loc->parent, POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;

green:
        if (xattr) {
                my_xattr = dict_ref (xattr);
        } else {
                my_xattr = dict_new ();
        }

        ret = dict_set_int8 (my_xattr, POSIX_ACL_ACCESS_XATTR, 0);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "failed to set key %s",
                        POSIX_ACL_ACCESS_XATTR);

        ret = dict_set_int8 (my_xattr, POSIX_ACL_DEFAULT_XATTR, 0);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "failed to set key %s",
                        POSIX_ACL_DEFAULT_XATTR);

        frame->local = my_xattr;
        STACK_WIND (frame, posix_acl_lookup_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->lookup,
                    loc, my_xattr);
        return 0;
red:
        STACK_UNWIND_STRICT (lookup, frame, -1, EACCES, NULL, NULL, NULL,
                             NULL);

        return 0;
}


int
posix_acl_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int mask,
                  dict_t *xdata)
{
        int  op_ret = 0;
        int  op_errno = 0;
        int  perm = 0;
        int  mode = 0;
        int  is_fuse_call = 0;

        is_fuse_call = __is_fuse_call (frame);

        if (mask & R_OK)
                perm |= POSIX_ACL_READ;
        if (mask & W_OK)
                perm |= POSIX_ACL_WRITE;
        if (mask & X_OK)
                perm |= POSIX_ACL_EXECUTE;
        if (!mask) {
                goto unwind;
        }
        if (!perm) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        if (is_fuse_call) {
                mode = acl_permits (frame, loc->inode, perm);
                if (mode) {
                        op_ret = 0;
                        op_errno = 0;
                } else {
                        op_ret = -1;
                        op_errno = EACCES;
                }
        } else {
                if (perm & POSIX_ACL_READ) {
                        if (acl_permits (frame, loc->inode, POSIX_ACL_READ))
                                mode |= POSIX_ACL_READ;
                }

                if (perm & POSIX_ACL_WRITE) {
                        if (acl_permits (frame, loc->inode, POSIX_ACL_WRITE))
                                mode |= POSIX_ACL_WRITE;
                }

                if (perm & POSIX_ACL_EXECUTE) {
                        if (acl_permits (frame, loc->inode, POSIX_ACL_EXECUTE))
                                mode |= POSIX_ACL_EXECUTE;
                }
        }

unwind:
        if (is_fuse_call)
                STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, NULL);
        else
                STACK_UNWIND_STRICT (access, frame, 0, mode, NULL);
        return 0;
}


int
posix_acl_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);

        return 0;
}


int
posix_acl_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t off,
                    dict_t *xdata)
{
        struct posix_acl_ctx *ctx = NULL;

        if (acl_permits (frame, loc->inode, POSIX_ACL_WRITE))
                goto green;
        /* NFS does a truncate through SETATTR, the owner does not need write
         * permissions for this. Group permissions and root are checked above.
         */
        else if (frame->root->pid == NFS_PID) {
                ctx = posix_acl_ctx_get (loc->inode, frame->this);

                if (ctx && frame_is_user (frame, ctx->uid))
                        goto green;
        }

        /* fail by default */
        STACK_UNWIND_STRICT (truncate, frame, -1, EACCES, NULL, NULL, NULL);
        return 0;

green:
        STACK_WIND (frame, posix_acl_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    loc, off, xdata);
        return 0;
}


int
posix_acl_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
posix_acl_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
                fd_t *fd, dict_t *xdata)
{
        int perm = 0;

        switch (flags & O_ACCMODE) {
        case O_RDONLY:
                perm = POSIX_ACL_READ;

                /* If O_FMODE_EXEC is present, its good enough
                   to have '--x' perm, and its not covered in
                   O_ACCMODE bits */
                if (flags & O_FMODE_EXEC)
                        perm = POSIX_ACL_EXECUTE;

                break;
        case O_WRONLY:
                perm = POSIX_ACL_WRITE;
                break;
        case O_RDWR:
                perm = POSIX_ACL_READ|POSIX_ACL_WRITE;
                break;
        }

        if (flags & (O_TRUNC | O_APPEND))
                perm |= POSIX_ACL_WRITE;

        if (acl_permits (frame, loc->inode, perm))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (open, frame, -1, EACCES, NULL, NULL);
        return 0;
}


int
posix_acl_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, struct iovec *vector,
                     int count, struct iatt *stbuf, struct iobref *iobref,
                     dict_t *xdata)
{
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref, xdata);
        return 0;
}


int
posix_acl_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        if (__is_fuse_call (frame))
                goto green;

        if (acl_permits (frame, fd->inode, POSIX_ACL_READ))
                goto green;
        else
                goto red;

green:
        STACK_WIND (frame, posix_acl_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (readv, frame, -1, EACCES, NULL, 0, NULL, NULL,
                             NULL);
        return 0;
}


int
posix_acl_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno,
                      struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
posix_acl_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iovec *vector, int count, off_t offset,
                  uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        if (__is_fuse_call (frame))
                goto green;

        if (acl_permits (frame, fd->inode, POSIX_ACL_WRITE))
                goto green;
        else
                goto red;

green:
        STACK_WIND (frame, posix_acl_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (writev, frame, -1, EACCES, NULL, NULL, NULL);
        return 0;
}



int
posix_acl_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata)
{
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
posix_acl_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset, dict_t *xdata)
{
        if (__is_fuse_call (frame))
                goto green;

        if (acl_permits (frame, fd->inode, POSIX_ACL_WRITE))
                goto green;
        else
                goto red;

green:
        STACK_WIND (frame, posix_acl_ftruncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (ftruncate, frame, -1, EACCES, NULL, NULL, NULL);
        return 0;
}


int
posix_acl_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
posix_acl_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd, dict_t *xdata)
{
        if (acl_permits (frame, loc->inode, POSIX_ACL_READ))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_opendir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->opendir,
                    loc, fd, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (opendir, frame, -1, EACCES, NULL, NULL);
        return 0;
}


int
posix_acl_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;

        posix_acl_ctx_update (inode, this, buf, GF_FOP_MKDIR);

unwind:
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                 mode_t umask, dict_t *xdata)
{
        mode_t   newmode = 0;

        newmode = mode;
        if (acl_permits (frame, loc->parent, POSIX_ACL_WRITE|POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;
green:
        newmode = posix_acl_inherit_dir (this, loc, xdata, mode, umask);

        STACK_WIND (frame, posix_acl_mkdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                    loc, newmode, umask, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (mkdir, frame, -1, EACCES, NULL, NULL, NULL, NULL,
                             NULL);
        return 0;
}


int
posix_acl_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;

        posix_acl_ctx_update (inode, this, buf, GF_FOP_MKNOD);

unwind:
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                 dev_t rdev, mode_t umask, dict_t *xdata)
{
        mode_t  newmode = 0;

        newmode = mode;
        if (acl_permits (frame, loc->parent, POSIX_ACL_WRITE|POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;
green:
        newmode = posix_acl_inherit_file (this, loc, xdata, mode, umask);

        STACK_WIND (frame, posix_acl_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, newmode, rdev, umask, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (mknod, frame, -1, EACCES, NULL, NULL, NULL, NULL,
                             NULL);
        return 0;
}


int
posix_acl_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, fd_t *fd, inode_t *inode,
                      struct iatt *buf, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;

        posix_acl_ctx_update (inode, this, buf, GF_FOP_CREATE);

unwind:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
                  mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        mode_t  newmode = 0;

        newmode = mode;
        if (acl_permits (frame, loc->parent, POSIX_ACL_WRITE|POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;
green:
        newmode = posix_acl_inherit_file (this, loc, xdata, mode, umask);

        STACK_WIND (frame, posix_acl_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, newmode, umask, fd, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (create, frame, -1, EACCES, NULL, NULL, NULL,
                             NULL, NULL, NULL);
        return 0;
}


int
posix_acl_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, inode_t *inode,
                      struct iatt *buf, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;

        posix_acl_ctx_update (inode, this, buf, GF_FOP_SYMLINK);

unwind:
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
                   loc_t *loc, mode_t umask, dict_t *xdata)
{
        if (acl_permits (frame, loc->parent, POSIX_ACL_WRITE|POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_symlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                    linkname, loc, umask, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (symlink, frame, -1, EACCES, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}


int
posix_acl_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;
unwind:
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                  dict_t *xdata)
{
        if (!sticky_permits (frame, loc->parent, loc->inode))
                goto red;

        if (acl_permits (frame, loc->parent, POSIX_ACL_WRITE|POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (unlink, frame, -1, EACCES, NULL, NULL, NULL);
        return 0;
}


int
posix_acl_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno,
                     struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;
unwind:
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
        if (!sticky_permits (frame, loc->parent, loc->inode))
                goto red;

        if (acl_permits (frame, loc->parent, POSIX_ACL_WRITE|POSIX_ACL_EXECUTE))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    loc, flags, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (rmdir, frame, -1, EACCES, NULL, NULL, NULL);
        return 0;
}


int
posix_acl_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;
unwind:
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);
        return 0;
}


int
posix_acl_rename (call_frame_t *frame, xlator_t *this, loc_t *old, loc_t *new, dict_t *xdata)
{
        if (!acl_permits (frame, old->parent, POSIX_ACL_WRITE))
                goto red;

        if (!acl_permits (frame, new->parent, POSIX_ACL_WRITE))
                goto red;

        if (!sticky_permits (frame, old->parent, old->inode))
                goto red;

        if (new->inode) {
                if (!sticky_permits (frame, new->parent, new->inode))
                        goto red;
        }

        STACK_WIND (frame, posix_acl_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    old, new, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (rename, frame, -1, EACCES, NULL, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}


int
posix_acl_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;
unwind:
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
posix_acl_link (call_frame_t *frame, xlator_t *this, loc_t *old, loc_t *new, dict_t *xdata)
{
        struct posix_acl_ctx *ctx = NULL;
        int                   op_errno = 0;

        ctx = posix_acl_ctx_get (old->inode, this);
        if (!ctx) {
                op_errno = EIO;
                goto red;
        }

        if (!acl_permits (frame, new->parent, POSIX_ACL_WRITE)) {
                op_errno = EACCES;
                goto red;
        }

        if (!sticky_permits (frame, new->parent, new->inode)) {
                op_errno = EACCES;
                goto red;
        }

        STACK_WIND (frame, posix_acl_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    old, new, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                             NULL);

        return 0;
}


int
posix_acl_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, gf_dirent_t *entries,
                       dict_t *xdata)
{
        if (op_ret != 0)
                goto unwind;
unwind:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries, xdata);
        return 0;
}


int
posix_acl_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                   off_t offset, dict_t *xdata)
{
        if (acl_permits (frame, fd->inode, POSIX_ACL_READ))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_readdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdir,
                    fd, size, offset, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (readdir, frame, -1, EACCES, NULL, NULL);

        return 0;
}


int
posix_acl_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, gf_dirent_t *entries,
                        dict_t *xdata)
{
        gf_dirent_t          *entry       = NULL;
        struct posix_acl     *acl_access  = NULL;
        struct posix_acl     *acl_default = NULL;
        struct posix_acl_ctx *ctx         = NULL;
        data_t               *data        = NULL;
        int                   ret         = 0;

        if (op_ret <= 0)
                goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                /* Update the inode ctx */
                if (!entry->dict || !entry->inode)
                        continue;

                ctx = posix_acl_ctx_new (entry->inode, this);
                if (!ctx) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = posix_acl_get (entry->inode, this,
                                     &acl_access, &acl_default);

                data = dict_get (entry->dict, POSIX_ACL_ACCESS_XATTR);
                if (!data)
                        goto acl_default;

                if (acl_access &&
                    posix_acl_matches_xattr (this, acl_access, data->data,
                                             data->len))
                        goto acl_default;

		if (acl_access)
			posix_acl_unref(this, acl_access);

                acl_access = posix_acl_from_xattr (this, data->data,
                                                   data->len);

        acl_default:
                data = dict_get (entry->dict, POSIX_ACL_DEFAULT_XATTR);
                if (!data)
                        goto acl_set;

                if (acl_default &&
                    posix_acl_matches_xattr (this, acl_default, data->data,
                                             data->len))
                        goto acl_set;

		if (acl_default)
			posix_acl_unref(this, acl_default);

                acl_default = posix_acl_from_xattr (this, data->data,
                                                    data->len);

        acl_set:
                posix_acl_ctx_update (entry->inode, this, &entry->d_stat,
                                      GF_FOP_READDIRP);

                ret = posix_acl_set (entry->inode, this,
                                     acl_access, acl_default);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set ACL in context");

		if (acl_access)
			posix_acl_unref(this, acl_access);
		if (acl_default)
			posix_acl_unref(this, acl_default);
        }

unwind:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
        return 0;
}


int
posix_acl_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                    off_t offset, dict_t *dict)
{
        int ret = 0;
	dict_t *alloc_dict = NULL;

        if (acl_permits (frame, fd->inode, POSIX_ACL_READ))
                goto green;
        else
                goto red;
green:
	if (!dict)
		dict = alloc_dict = dict_new ();

        if (dict) {
                ret = dict_set_int8 (dict, POSIX_ACL_ACCESS_XATTR, 0);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set key %s",
                                POSIX_ACL_ACCESS_XATTR);

                ret = dict_set_int8 (dict, POSIX_ACL_DEFAULT_XATTR, 0);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set key %s",
                                POSIX_ACL_DEFAULT_XATTR);
        }

        STACK_WIND (frame, posix_acl_readdirp_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset, dict);

	if (alloc_dict)
		dict_unref (alloc_dict);
        return 0;
red:
        STACK_UNWIND_STRICT (readdirp, frame, -1, EACCES, NULL, NULL);

        return 0;
}


int
setattr_scrutiny (call_frame_t *frame, inode_t *inode, struct iatt *buf,
                  int valid)
{
        struct posix_acl_ctx   *ctx = NULL;

        if (frame_is_super_user (frame))
                return 0;

        ctx = posix_acl_ctx_get (inode, frame->this);
        if (!ctx)
                return EIO;

        if (valid & GF_SET_ATTR_MODE) {
/*
       The effective UID of the calling process must match the  owner  of  the
       file,  or  the  process  must  be  privileged
*/
                if (!frame_is_user (frame, ctx->uid))
                        return EPERM;
/*
       If the calling process is not privileged  (Linux:  does  not  have  the
       CAP_FSETID  capability),  and  the group of the file does not match the
       effective group ID of the process or one  of  its  supplementary  group
       IDs,  the  S_ISGID  bit  will be turned off, but this will not cause an
       error to be returned.

*/
                if (!frame_in_group (frame, ctx->gid))
                        buf->ia_prot.sgid = 0;
        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
/*
       Changing timestamps is permitted when: either the process has appropri?
       ate  privileges,  or  the  effective  user ID equals the user ID of the
       file, or times is NULL and the process has  write  permission  for  the
       file.
*/
                if (!frame_is_user (frame, ctx->uid) &&
                    !acl_permits (frame, inode, POSIX_ACL_WRITE))
                        return EACCES;
        }

        if (valid & GF_SET_ATTR_UID) {
                if ((!frame_is_super_user (frame)) &&
                    (buf->ia_uid != ctx->uid))
                        return EPERM;
        }

        if (valid & GF_SET_ATTR_GID) {
                if (!frame_is_user (frame, ctx->uid))
                        return EPERM;
                if (!frame_in_group (frame, buf->ia_gid))
                        return EPERM;
        }

        return 0;
}


int
posix_acl_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno,
                       struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        inode_t   *inode = NULL;

        inode = frame->local;
        frame->local = NULL;

        if (op_ret != 0)
                goto unwind;

        posix_acl_ctx_update (inode, this, postbuf, GF_FOP_SETATTR);

unwind:
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


int
posix_acl_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *buf, int valid, dict_t *xdata)
{
        int  op_errno = 0;

        op_errno = setattr_scrutiny (frame, loc->inode, buf, valid);

        if (op_errno)
                goto red;

        frame->local = loc->inode;

        STACK_WIND (frame, posix_acl_setattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setattr,
                    loc, buf, valid, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int
posix_acl_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno,
                        struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        inode_t   *inode = NULL;

        inode = frame->local;
        frame->local = NULL;

        if (op_ret != 0)
                goto unwind;

        posix_acl_ctx_update (inode, this, postbuf, GF_FOP_FSETATTR);

unwind:
        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


int
posix_acl_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *buf, int valid, dict_t *xdata)
{
        int  op_errno = 0;

        op_errno = setattr_scrutiny (frame, fd->inode, buf, valid);

        if (op_errno)
                goto red;

        frame->local = fd->inode;

        STACK_WIND (frame, posix_acl_fsetattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetattr,
                    fd, buf, valid, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (fsetattr, frame, -1, EACCES, NULL, NULL, NULL);

        return 0;
}


int
setxattr_scrutiny (call_frame_t *frame, inode_t *inode, dict_t *xattr)
{
        struct posix_acl_ctx   *ctx = NULL;
        int                     found = 0;

        if (frame_is_super_user (frame))
                return 0;

        ctx = posix_acl_ctx_get (inode, frame->this);
        if (!ctx)
                return EIO;

        if (dict_get (xattr, POSIX_ACL_ACCESS_XATTR)) {
                found = 1;
                if (!frame_is_user (frame, ctx->uid))
                        return EPERM;
        }

        if (dict_get (xattr, POSIX_ACL_DEFAULT_XATTR)) {
                found = 1;
                if (!frame_is_user (frame, ctx->uid))
                        return EPERM;
        }

        if (!found && !acl_permits (frame, inode, POSIX_ACL_WRITE))
                return EACCES;

        return 0;
}


struct posix_acl *
posix_acl_xattr_update (xlator_t *this, inode_t *inode, dict_t *xattr,
                        char *name, struct posix_acl *old)
{
        struct  posix_acl      *acl = NULL;
        data_t                 *data = NULL;

        data = dict_get (xattr, name);
        if (data) {
                acl = posix_acl_from_xattr (this, data->data,
                                            data->len);
        }

        if (!acl && old)
                acl = posix_acl_ref (this, old);

        return acl;
}


int
posix_acl_setxattr_update (xlator_t *this, inode_t *inode, dict_t *xattr)
{
        struct posix_acl     *acl_access = NULL;
        struct posix_acl     *acl_default = NULL;
        struct posix_acl     *old_access = NULL;
        struct posix_acl     *old_default = NULL;
        struct posix_acl_ctx *ctx = NULL;
        int                   ret = 0;

        ctx = posix_acl_ctx_get (inode, this);
        if (!ctx)
                return -1;

        ret = posix_acl_get (inode, this, &old_access, &old_default);

        acl_access = posix_acl_xattr_update (this, inode, xattr,
                                             POSIX_ACL_ACCESS_XATTR,
                                             old_access);

        acl_default = posix_acl_xattr_update (this, inode, xattr,
                                              POSIX_ACL_DEFAULT_XATTR,
                                              old_default);

        ret = posix_acl_set (inode, this, acl_access, acl_default);

        if (acl_access && acl_access != old_access) {
                posix_acl_access_set_mode (acl_access, ctx);
        }

        if (acl_access)
                posix_acl_unref (this, acl_access);
        if (acl_default)
                posix_acl_unref (this, acl_default);
        if (old_access)
                posix_acl_unref (this, old_access);
        if (old_default)
                posix_acl_unref (this, old_default);

        return ret;
}

/* *
 * Posix acl can be set using other xattr such as GF_POSIX_ACL_ACCESS/
 * GF_POSIX_ACL_DEFAULT  which requires to update the context of
 * access-control translator
 */
int
handling_other_acl_related_xattr (xlator_t *this, inode_t *inode, dict_t *xattr)
{
        struct posix_acl       *acl      = NULL;
        struct posix_acl_ctx   *ctx      = NULL;
        data_t                 *data     = NULL;
        int                     ret      = 0;

        if (!this || !xattr || !inode)
                goto out;

        data = dict_get (xattr, POSIX_ACL_ACCESS_XATTR);
        if (data) {

                acl = posix_acl_from_xattr (this, data->data, data->len);
                if (!acl) {
                        ret = -1;
                        goto out;
                }

                ret = posix_acl_set_specific (inode, this, acl, _gf_true);
                if (ret)
                        goto out;

                ctx = posix_acl_ctx_get (inode, this);
                if (!ctx) {
                        ret = -1;
                        goto out;
                }

                posix_acl_access_set_mode (acl, ctx);
                posix_acl_unref (this, acl);
                acl = NULL;
        }

        data = dict_get (xattr, POSIX_ACL_DEFAULT_XATTR);
        if (data) {
                acl = posix_acl_from_xattr (this, data->data, data->len);
                if (!acl) {
                        ret = -1;
                        goto out;
                }

                ret = posix_acl_set_specific (inode, this, acl, _gf_false);
        }

out:
        if (acl)
                posix_acl_unref (this, acl);

        return ret;
}
int
posix_acl_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xdata)
{

        /*
         * Update the context of posix_acl_translator, if any of
         * POSIX_ACL_*_XATTR set in the call back
         */
        handling_other_acl_related_xattr (this, (inode_t *)cookie, xdata);

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
posix_acl_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xattr, int flags, dict_t *xdata)
{
        int  op_errno = 0;

        op_errno = setxattr_scrutiny (frame, loc->inode, xattr);

        if (op_errno != 0)
                goto red;

        posix_acl_setxattr_update (this, loc->inode, xattr);

        /*
         * inode is required in call back function to update the context
         * this translator
         */
        STACK_WIND_COOKIE (frame, posix_acl_setxattr_cbk, loc->inode,
                           FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr,
                           loc, xattr, flags, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno, NULL);

        return 0;
}


int
posix_acl_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
posix_acl_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     dict_t *xattr, int flags, dict_t *xdata)
{
        int  op_errno = 0;

        op_errno = setxattr_scrutiny (frame, fd->inode, xattr);

        if (op_errno != 0)
                goto red;

        posix_acl_setxattr_update (this, fd->inode, xattr);

        STACK_WIND (frame, posix_acl_fsetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetxattr,
                    fd, xattr, flags, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, op_errno, NULL);

        return 0;
}


int
posix_acl_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, xattr, xdata);

        return 0;
}


int
posix_acl_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name, dict_t *xdata)
{
        if (whitelisted_xattr (name))
                goto green;

        if (acl_permits (frame, loc->inode, POSIX_ACL_READ))
                goto green;
        else
                goto red;

green:
        STACK_WIND (frame, posix_acl_getxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->getxattr,
                    loc, name, xdata);
        return 0;

red:
        STACK_UNWIND_STRICT (getxattr, frame, -1, EACCES, NULL, NULL);

        return 0;
}


int
posix_acl_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, xattr, xdata);

        return 0;
}


int
posix_acl_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name, dict_t *xdata)
{
        if (whitelisted_xattr (name))
                goto green;

        if (acl_permits (frame, fd->inode, POSIX_ACL_READ))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_fgetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fgetxattr,
                    fd, name, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (fgetxattr, frame, -1, EACCES, NULL, NULL);

        return 0;
}


int
posix_acl_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
posix_acl_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       const char *name, dict_t *xdata)
{
        struct  posix_acl_ctx  *ctx = NULL;
        int                     op_errno = EACCES;

        if (frame_is_super_user (frame))
                goto green;

        ctx = posix_acl_ctx_get (loc->inode, this);
        if (!ctx) {
                op_errno = EIO;
                goto red;
        }

        if (whitelisted_xattr (name)) {
                if (!frame_is_user (frame, ctx->uid)) {
                        op_errno = EPERM;
                        goto red;
                }
        }

        if (acl_permits (frame, loc->inode, POSIX_ACL_WRITE))
                goto green;
        else
                goto red;
green:
        STACK_WIND (frame, posix_acl_removexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
red:
        STACK_UNWIND_STRICT (removexattr, frame, -1, op_errno, NULL);

        return 0;
}


int
posix_acl_forget (xlator_t *this, inode_t *inode)
{
        struct posix_acl_ctx *ctx = NULL;

        ctx = posix_acl_ctx_get (inode, this);
        if (!ctx)
                goto out;

        if (ctx->acl_access)
                posix_acl_unref (this, ctx->acl_access);

        if (ctx->acl_default)
                posix_acl_unref (this, ctx->acl_default);

        GF_FREE (ctx);
out:
        return 0;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
        struct posix_acl_conf *conf = NULL;

        conf = this->private;

        GF_OPTION_RECONF ("super-uid", conf->super_uid, options, uint32, err);

        return 0;
err:
        return -1;
}


int
init (xlator_t *this)
{
        struct posix_acl_conf   *conf = NULL;
        struct posix_acl        *minacl = NULL;
        struct posix_ace        *minace = NULL;

        conf = GF_CALLOC (1, sizeof (*conf), gf_posix_acl_mt_conf_t);
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                return -1;
        }

        LOCK_INIT (&conf->acl_lock);

        this->private = conf;

        minacl = posix_acl_new (this, 3);
        if (!minacl)
                return -1;

        minace = minacl->entries;
        minace[0].tag = POSIX_ACL_USER_OBJ;
        minace[1].tag = POSIX_ACL_GROUP_OBJ;
        minace[2].tag = POSIX_ACL_OTHER;

        conf->minimal_acl = minacl;

        GF_OPTION_INIT ("super-uid", conf->super_uid, uint32, err);

        return 0;
err:
        return -1;
}


int
fini (xlator_t *this)
{
        struct posix_acl_conf   *conf = NULL;
        struct posix_acl        *minacl = NULL;

        conf = this->private;
        if (!conf)
                return 0;
        this->private = NULL;

        minacl = conf->minimal_acl;

        LOCK (&conf->acl_lock);
        {
                conf->minimal_acl = NULL;
        }
        UNLOCK (&conf->acl_lock);

        LOCK_DESTROY (&conf->acl_lock);

        GF_FREE (minacl);

        GF_FREE (conf);

        return 0;
}


struct xlator_fops fops = {
        .lookup           = posix_acl_lookup,
        .open             = posix_acl_open,
#if FD_MODE_CHECK_IS_IMPLEMENTED
        .readv            = posix_acl_readv,
        .writev           = posix_acl_writev,
        .ftruncate        = posix_acl_ftruncate,
        .fsetattr         = posix_acl_fsetattr,
        .fsetxattr        = posix_acl_fsetxattr,
        .fgetxattr        = posix_acl_fgetxattr,
#endif
        .access           = posix_acl_access,
        .truncate         = posix_acl_truncate,
        .mkdir            = posix_acl_mkdir,
        .mknod            = posix_acl_mknod,
        .create           = posix_acl_create,
        .symlink          = posix_acl_symlink,
        .unlink           = posix_acl_unlink,
        .rmdir            = posix_acl_rmdir,
        .rename           = posix_acl_rename,
        .link             = posix_acl_link,
        .opendir          = posix_acl_opendir,
        .readdir          = posix_acl_readdir,
        .readdirp         = posix_acl_readdirp,
        .setattr          = posix_acl_setattr,
        .setxattr         = posix_acl_setxattr,
        .getxattr         = posix_acl_getxattr,
        .removexattr      = posix_acl_removexattr,
};


struct xlator_cbks cbks = {
        .forget           = posix_acl_forget
};


struct volume_options options[] = {
        { .key  = {"super-uid"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "0",
          .description = "UID to be treated as super user's id instead of 0",
        },
        { .key = {NULL} },
};
