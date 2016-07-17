/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "gfid-access.h"
#include "inode.h"
#include "byte-order.h"
#include "statedump.h"


int
ga_valid_inode_loc_copy (loc_t *dst, loc_t *src, xlator_t *this)
{
        int      ret        = 0;
        uint64_t value      = 0;

        /* if its an entry operation, on the virtual */
        /* directory inode as parent, we need to handle */
        /* it properly */
        ret = loc_copy (dst, src);
        if (ret < 0)
                goto out;

        /*
         * Change ALL virtual inodes with real-inodes in loc
         */
        if (dst->parent) {
                ret = inode_ctx_get (dst->parent, this, &value);
                if (ret < 0) {
                        ret = 0; //real-inode
                        goto out;
                }
                inode_unref (dst->parent);
                dst->parent = inode_ref ((inode_t*)value);
                gf_uuid_copy (dst->pargfid, dst->parent->gfid);
        }

        if (dst->inode) {
                ret = inode_ctx_get (dst->inode, this, &value);
                if (ret < 0) {
                        ret = 0; //real-inode
                        goto out;
                }
                inode_unref (dst->inode);
                dst->inode = inode_ref ((inode_t*)value);
                gf_uuid_copy (dst->gfid, dst->inode->gfid);
        }
out:

        return ret;
}

void
ga_newfile_args_free (ga_newfile_args_t *args)
{
        if (!args)
                goto out;

        GF_FREE (args->bname);

        if (S_ISLNK (args->st_mode) && args->args.symlink.linkpath) {
                GF_FREE (args->args.symlink.linkpath);
                args->args.symlink.linkpath = NULL;
        }

        mem_put (args);
out:
        return;
}


void
ga_heal_args_free (ga_heal_args_t *args)
{
        if (!args)
                goto out;

        GF_FREE (args->bname);

        mem_put (args);
out:
        return;
}


ga_newfile_args_t *
ga_newfile_parse_args (xlator_t *this, data_t *data)
{
        ga_newfile_args_t *args     = NULL;
        ga_private_t      *priv     = NULL;
        int                len      = 0;
        int                blob_len = 0;
        int                min_len  = 0;
        void              *blob     = NULL;

        priv = this->private;

        blob = data->data;
        blob_len = data->len;

        min_len = sizeof (args->uid) + sizeof (args->gid) + sizeof (args->gfid)
                + sizeof (args->st_mode) + 2 + 2;
        if (blob_len < min_len) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid length: Total length is less "
                        "than minimum length.");
                goto err;
        }

        args = mem_get0 (priv->newfile_args_pool);
        if (args == NULL)
                goto err;

        args->uid = ntoh32 (*(uint32_t *)blob);
        blob += sizeof (uint32_t);
        blob_len -= sizeof (uint32_t);

        args->gid = ntoh32 (*(uint32_t *)blob);
        blob += sizeof (uint32_t);
        blob_len -= sizeof (uint32_t);

        memcpy (args->gfid, blob, sizeof (args->gfid));
        blob += sizeof (args->gfid);
        blob_len -= sizeof (args->gfid);

        args->st_mode = ntoh32 (*(uint32_t *)blob);
        blob += sizeof (uint32_t);
        blob_len -= sizeof (uint32_t);

        len = strnlen (blob, blob_len);
        if (len == blob_len) {
                gf_log (this->name, GF_LOG_ERROR,
                        "gfid: %s. No null byte present.",
                        args->gfid);
                goto err;
        }

        args->bname = GF_CALLOC (1, (len + 1), gf_common_mt_char);
        if (args->bname == NULL)
                goto err;

        memcpy (args->bname, blob, (len + 1));
        blob += (len + 1);
        blob_len -= (len + 1);

        if (S_ISDIR (args->st_mode)) {
                if (blob_len < sizeof (uint32_t)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
                args->args.mkdir.mode = ntoh32 (*(uint32_t *)blob);
                blob += sizeof (uint32_t);
                blob_len -= sizeof (uint32_t);

                if (blob_len < sizeof (uint32_t)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
                args->args.mkdir.umask = ntoh32 (*(uint32_t *)blob);
                blob_len -= sizeof (uint32_t);
                if (blob_len < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
        } else if (S_ISLNK (args->st_mode)) {
                len = strnlen (blob, blob_len);
                if (len == blob_len) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
                args->args.symlink.linkpath = GF_CALLOC (1, len + 1,
                                                         gf_common_mt_char);
                if (args->args.symlink.linkpath == NULL)
                        goto err;

                memcpy (args->args.symlink.linkpath, blob, (len + 1));
                blob_len -= (len + 1);
        } else {
                if (blob_len < sizeof (uint32_t)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
                args->args.mknod.mode = ntoh32 (*(uint32_t *)blob);
                blob += sizeof (uint32_t);
                blob_len -= sizeof (uint32_t);

                if (blob_len < sizeof (uint32_t)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
                args->args.mknod.rdev = ntoh32 (*(uint32_t *)blob);
                blob += sizeof (uint32_t);
                blob_len -= sizeof (uint32_t);

                if (blob_len < sizeof (uint32_t)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "gfid: %s. Invalid length",
                                args->gfid);
                        goto err;
                }
                args->args.mknod.umask = ntoh32 (*(uint32_t *)blob);
                blob_len -= sizeof (uint32_t);
        }

        if (blob_len) {
                gf_log (this->name, GF_LOG_ERROR,
                        "gfid: %s. Invalid length",
                        args->gfid);
                goto err;
        }

        return args;

err:
        if (args)
                ga_newfile_args_free (args);

        return NULL;
}

ga_heal_args_t *
ga_heal_parse_args (xlator_t *this, data_t *data)
{
        ga_heal_args_t *args     = NULL;
        ga_private_t   *priv     = NULL;
        void           *blob     = NULL;
        int             len      = 0;
        int             blob_len = 0;

        blob = data->data;
        blob_len = data->len;

        priv = this->private;

        /* bname should at least contain a character */
        if (blob_len < (sizeof (args->gfid) + 2))
                goto err;

        args = mem_get0 (priv->heal_args_pool);
        if (!args)
                goto err;

        memcpy (args->gfid, blob, sizeof (args->gfid));
        blob += sizeof (args->gfid);
        blob_len -= sizeof (args->gfid);

        len = strnlen (blob, blob_len);
        if (len == blob_len)
                goto err;

        args->bname = GF_CALLOC (1, len + 1, gf_common_mt_char);
        if (!args->bname)
                goto err;

        memcpy (args->bname, blob, len);
        blob_len -= (len + 1);

        if (blob_len)
                goto err;

        return args;

err:
        if (args)
                ga_heal_args_free (args);

        return NULL;
}

static int32_t
ga_fill_tmp_loc (loc_t *loc, xlator_t *this, uuid_t gfid,
                 char *bname, dict_t *xdata, loc_t *new_loc)
{
        int       ret    = -1;
        uint64_t  value  = 0;
        inode_t  *parent = NULL;
        uuid_t *gfid_ptr = NULL;

        parent = loc->inode;
        ret = inode_ctx_get (loc->inode, this, &value);
        if (!ret) {
                parent = (void *)value;
                if (gf_uuid_is_null (parent->gfid))
                        parent = loc->inode;
        }

        /* parent itself should be looked up */
        gf_uuid_copy (new_loc->pargfid, parent->gfid);
        new_loc->parent = inode_ref (parent);

        new_loc->inode = inode_grep (parent->table, parent, bname);
        if (!new_loc->inode) {
                new_loc->inode = inode_new (parent->table);
                gf_uuid_copy (new_loc->inode->gfid, gfid);
        }

        loc_path (new_loc, bname);
        if (new_loc->path) {
                new_loc->name = strrchr (new_loc->path, '/');
                if (new_loc->name)
                        new_loc->name++;
        }

        gfid_ptr = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!gfid_ptr) {
                ret = -1;
                goto out;
        }
        gf_uuid_copy (*gfid_ptr, gfid);
        ret = dict_set_dynptr (xdata, "gfid-req", gfid_ptr, sizeof (uuid_t));
        if (ret < 0)
                goto out;

        ret = 0;

out:
        if (ret && gfid_ptr)
                GF_FREE (gfid_ptr);
        return ret;
}



static gf_boolean_t
__is_gfid_access_dir (uuid_t gfid)
{
        uuid_t  aux_gfid;

        memset (aux_gfid, 0, 16);
        aux_gfid[15] = GF_AUX_GFID;

        if (gf_uuid_compare (gfid, aux_gfid) == 0)
                return _gf_true;

        return _gf_false;
}

int32_t
ga_forget (xlator_t *this, inode_t *inode)
{
        int       ret = -1;
        uint64_t  value = 0;
        inode_t  *tmp_inode = NULL;

        ret = inode_ctx_del (inode, this, &value);
        if (ret)
                goto out;

        tmp_inode = (void *)value;
        inode_unref (tmp_inode);

out:
        return 0;
}


static int
ga_heal_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno,
             inode_t *inode, struct iatt *stat, dict_t *dict,
             struct iatt *postparent)
{
        call_frame_t *orig_frame = NULL;

        orig_frame = frame->local;
        frame->local = NULL;

        /* don't worry about inode linking and other stuff. They'll happen on
         * the next lookup.
         */
        STACK_DESTROY (frame->root);

        STACK_UNWIND_STRICT (setxattr, orig_frame, op_ret, op_errno, dict);

        return 0;
}

static int
ga_newentry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct iatt *buf,
                 struct iatt *preparent, struct iatt *postparent,
                 dict_t *xdata)
{
        ga_local_t *local = NULL;

        local = frame->local;

        /* don't worry about inode linking and other stuff. They'll happen on
         * the next lookup.
         */
        frame->local = NULL;
        STACK_DESTROY (frame->root);

        STACK_UNWIND_STRICT (setxattr, local->orig_frame, op_ret,
                             op_errno, xdata);

        if (local->xdata)
                dict_unref (local->xdata);
        loc_wipe (&local->loc);
        mem_put (local);

        return 0;
}

static int
ga_newentry_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *stat, dict_t *xdata,
                        struct iatt *postparent)

{
        ga_local_t *local = NULL;

        local = frame->local;

        if ((op_ret < 0) && ((op_errno != ENOENT) && (op_errno != ESTALE)))
                goto err;

        STACK_WIND (frame, ga_newentry_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, &local->loc, local->mode,
                    local->rdev, local->umask, local->xdata);
        return 0;

err:
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        STACK_UNWIND_STRICT (setxattr, local->orig_frame, op_ret, op_errno,
                             xdata);
        if (local->xdata)
                dict_unref (local->xdata);
        loc_wipe (&local->loc);
        mem_put (local);

        return 0;
}

int32_t
ga_new_entry (call_frame_t *frame, xlator_t *this, loc_t *loc, data_t *data,
              dict_t *xdata)
{
        int                ret       = -1;
        ga_newfile_args_t *args      = NULL;
        loc_t              tmp_loc   = {0,};
        call_frame_t      *new_frame = NULL;
        ga_local_t        *local     = NULL;
        uuid_t             gfid      = {0,};

        args = ga_newfile_parse_args (this, data);
        if (!args)
                goto out;

        ret = gf_uuid_parse (args->gfid, gfid);
        if (ret)
                goto out;

        if (!xdata) {
                xdata = dict_new ();
        } else {
                xdata = dict_ref (xdata);
        }

        if (!xdata) {
                ret = -1;
                goto out;
        }

        ret = ga_fill_tmp_loc (loc, this, gfid,
                               args->bname, xdata, &tmp_loc);
        if (ret)
                goto out;

        new_frame = copy_frame (frame);
        if (!new_frame)
                goto out;

        local = mem_get0 (this->local_pool);
        local->orig_frame = frame;

        loc_copy (&local->loc, &tmp_loc);

        new_frame->local = local;
        new_frame->root->uid = args->uid;
        new_frame->root->gid = args->gid;

        if (S_ISDIR (args->st_mode)) {
                STACK_WIND (new_frame, ga_newentry_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                            &tmp_loc, args->args.mkdir.mode,
                            args->args.mkdir.umask, xdata);
        } else if (S_ISLNK (args->st_mode)) {
                STACK_WIND (new_frame, ga_newentry_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                            args->args.symlink.linkpath,
                            &tmp_loc, 0, xdata);
        } else {
                /* use 07777 (4 7s) for considering the Sticky bits etc) */
                ((ga_local_t *)new_frame->local)->mode =
                     (S_IFMT & args->st_mode) | (07777 & args->args.mknod.mode);

                ((ga_local_t *)new_frame->local)->umask =
                                                         args->args.mknod.umask;
                ((ga_local_t *)new_frame->local)->rdev  = args->args.mknod.rdev;
                ((ga_local_t *)new_frame->local)->xdata = dict_ref (xdata);

                /* send a named lookup, so that dht can cleanup up stale linkto
                 * files etc.
                 */
                STACK_WIND (new_frame, ga_newentry_lookup_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup,
                            &tmp_loc, NULL);
        }

        ret = 0;
out:
        ga_newfile_args_free (args);

        if (xdata)
                dict_unref (xdata);

        loc_wipe (&tmp_loc);

        return ret;
}

int32_t
ga_heal_entry (call_frame_t *frame, xlator_t *this, loc_t *loc, data_t *data,
               dict_t *xdata)
{
        int             ret       = -1;
        ga_heal_args_t *args      = NULL;
        loc_t           tmp_loc   = {0,};
        call_frame_t   *new_frame = NULL;
        uuid_t          gfid      = {0,};

        args = ga_heal_parse_args (this, data);
        if (!args)
                goto out;

        ret = gf_uuid_parse (args->gfid, gfid);
        if (ret)
                goto out;

        if (!xdata)
                xdata = dict_new ();
        else
                xdata = dict_ref (xdata);

        if (!xdata) {
                ret = -1;
                goto out;
        }

        ret = ga_fill_tmp_loc (loc, this, gfid, args->bname,
                               xdata, &tmp_loc);
        if (ret)
                goto out;

        new_frame = copy_frame (frame);
        if (!new_frame)
                goto out;

        new_frame->local = (void *)frame;

        STACK_WIND (new_frame, ga_heal_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->lookup,
                    &tmp_loc, xdata);

        ret = 0;
out:
        if (args)
                ga_heal_args_free (args);

        loc_wipe (&tmp_loc);

        if (xdata)
                dict_unref (xdata);

        return ret;
}

int32_t
ga_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 dict_t *xdata)
{
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
ga_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
        data_t  *data     = NULL;
        int      op_errno = ENOMEM;
        int      ret      = 0;
        loc_t   ga_loc    = {0, };

        GFID_ACCESS_INODE_OP_CHECK (loc, op_errno, err);

        data = dict_get (dict, GF_FUSE_AUX_GFID_NEWFILE);
        if (data) {
                ret = ga_new_entry (frame, this, loc, data, xdata);
                if (ret)
                        goto err;
                return 0;
        }

        data = dict_get (dict, GF_FUSE_AUX_GFID_HEAL);
        if (data) {
                ret = ga_heal_entry (frame, this, loc, data, xdata);
                if (ret)
                        goto err;
                return 0;
        }

        //If the inode is a virtual inode change the inode otherwise perform
        //the operation on same inode
        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, ga_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, &ga_loc, dict, flags,
                    xdata);

        loc_wipe (&ga_loc);
        return 0;
err:
        STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno, xdata);
        return 0;
}


int32_t
ga_virtual_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, inode_t *inode,
                       struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        int             ret                     = 0;
        inode_t         *cbk_inode              = NULL;
        inode_t         *true_inode             = NULL;
        uuid_t          random_gfid             = {0,};
        inode_t         *linked_inode           = NULL;

        if (frame->local)
                cbk_inode = frame->local;
        else
                cbk_inode = inode_ref (inode);

        frame->local = NULL;
        if (op_ret)
                goto unwind;

        if (!IA_ISDIR (buf->ia_type))
                goto unwind;

        /* need to send back a different inode for linking in itable */
        if (cbk_inode == inode) {
                /* check if the inode is in the 'itable' or
                   if its just previously discover()'d inode */
                true_inode = inode_find (inode->table, buf->ia_gfid);
                if (!true_inode) {
                        /* This unref is for 'inode_ref()' done in beginning.
                           This is needed as cbk_inode is allocated new inode
                           whose unref is taken at the end*/
                        inode_unref (cbk_inode);
                        cbk_inode = inode_new (inode->table);

                        if (!cbk_inode) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unwind;
                        }
                        /* the inode is not present in itable, ie, the actual
                           path is not yet looked up. Use the current inode
                           itself for now */

                        linked_inode = inode_link (inode, NULL, NULL, buf);
                        inode = linked_inode;
                } else {
                        /* 'inode_ref()' has been done in inode_find() */
                        inode = true_inode;
                }

                ret = inode_ctx_put (cbk_inode, this, (uint64_t)inode);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set the inode ctx with"
                                "the actual inode");
                        if (inode)
                                inode_unref (inode);
                }
                inode = NULL;
        }

        if (!gf_uuid_is_null (cbk_inode->gfid)) {
                /* if the previous linked inode is used, use the
                   same gfid */
                gf_uuid_copy (random_gfid, cbk_inode->gfid);
        } else {
                /* replace the buf->ia_gfid to a random gfid
                   for directory, for files, what we received is fine */
                gf_uuid_generate (random_gfid);
        }

        gf_uuid_copy (buf->ia_gfid, random_gfid);

        buf->ia_ino = gfid_to_ino (buf->ia_gfid);

unwind:
        /* Lookup on non-existing gfid returns ESTALE.
           Convert into ENOENT for virtual lookup*/
        if (op_errno == ESTALE)
               op_errno = ENOENT;

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, cbk_inode, buf,
                             xdata, postparent);

        /* Also handles inode_unref of frame->local if done in ga_lookup */
        if (cbk_inode)
               inode_unref (cbk_inode);

        return 0;
}

int32_t
ga_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        ga_private_t *priv = NULL;

        /* if the entry in question is not 'root',
           then follow the normal path */
        if (op_ret || !__is_root_gfid(buf->ia_gfid))
                goto unwind;

        priv = this->private;

        /* do we need to copy root stbuf everytime? */
        /* mostly yes, as we want to have the 'stat' info show latest
           in every _cbk() */

        /* keep the reference for root stat buf */
        priv->root_stbuf = *buf;
        priv->gfiddir_stbuf = priv->root_stbuf;
        priv->gfiddir_stbuf.ia_gfid[15] = GF_AUX_GFID;
        priv->gfiddir_stbuf.ia_ino = GF_AUX_GFID;

unwind:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        return 0;
}

int32_t
ga_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        ga_private_t *priv     = NULL;
        int           ret      = -1;
        uuid_t        tmp_gfid = {0,};
        loc_t         tmp_loc  = {0,};
        uint64_t      value    = 0;
        inode_t      *inode    = NULL;
        inode_t      *true_inode    = NULL;
        int32_t       op_errno = ENOENT;

        priv = this->private;

        /* Handle nameless lookup on ".gfid" */
        if (!loc->parent && __is_gfid_access_dir(loc->gfid)) {
                STACK_UNWIND_STRICT (lookup, frame, 0, 0, loc->inode,
                                     &priv->gfiddir_stbuf, xdata,
                                     &priv->root_stbuf);
                return 0;
        }

        /* if its discover(), no need for any action here */
        if (!loc->name)
                goto wind;

        /* if its revalidate, and inode is not of type directory,
           proceed with 'wind' */
        if (loc->inode && loc->inode->ia_type &&
            !IA_ISDIR (loc->inode->ia_type)) {

                /* a revalidate on ".gfid/<dentry>" is possible, check for it */
                if (((loc->parent &&
                      __is_gfid_access_dir (loc->parent->gfid)) ||
                     __is_gfid_access_dir (loc->pargfid))) {

                        /* here, just send 'loc->gfid' and 'loc->inode' */
                        tmp_loc.inode = inode_ref (loc->inode);
                        gf_uuid_copy (tmp_loc.gfid, loc->inode->gfid);

                        STACK_WIND (frame, default_lookup_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->lookup,
                                    &tmp_loc, xdata);

                        inode_unref (tmp_loc.inode);

                        return 0;
                }

                /* not something to bother, continue the flow */
                goto wind;
        }

        /* need to check if the lookup is on virtual dir */
        if ((loc->name && !strcmp (GF_GFID_DIR, loc->name)) &&
            ((loc->parent && __is_root_gfid (loc->parent->gfid)) ||
             __is_root_gfid (loc->pargfid))) {
                /* this means, the query is on '/.gfid', return the fake stat,
                   and say success */

                STACK_UNWIND_STRICT (lookup, frame, 0, 0, loc->inode,
                                     &priv->gfiddir_stbuf, xdata,
                                     &priv->root_stbuf);
                return 0;
        }

        /* now, check if the lookup() is on an existing entry,
           but on gfid-path */
        if (!((loc->parent && __is_gfid_access_dir (loc->parent->gfid)) ||
              __is_gfid_access_dir (loc->pargfid))) {
                if (!loc->parent)
                        goto wind;

                ret = inode_ctx_get (loc->parent, this, &value);
                if (ret)
                        goto wind;

                inode = (inode_t *) value;

                ret = loc_copy_overload_parent (&tmp_loc, loc, inode);
                if (ret)
                        goto err;

                STACK_WIND (frame, ga_lookup_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->lookup, &tmp_loc, xdata);

                loc_wipe (&tmp_loc);
                return 0;
        }

        /* make sure the 'basename' is actually a 'canonical-gfid',
           otherwise, return error */
        ret = gf_uuid_parse (loc->name, tmp_gfid);
        if (ret)
                goto err;

        /* if its fresh lookup, go ahead and send it down, if not,
           for directory, we need indirection to actual dir inode */
        if (!(loc->inode && loc->inode->ia_type))
                goto discover;

        /* revalidate on directory */
        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret)
                goto err;

        inode = (void *)value;

        /* valid inode, already looked up, work on that */
        if (inode->ia_type)
                goto discover;

        /* check if the inode is in the 'itable' or
           if its just previously discover()'d inode */
        true_inode = inode_find (loc->inode->table, tmp_gfid);
        if (true_inode) {
                /* time do another lookup and update the context
                   with proper inode */
                op_errno = ESTALE;
                /* 'inode_ref()' done in inode_find */
                inode_unref (true_inode);
                goto err;
        }

discover:
        /* for the virtual entries, we don't need to send 'gfid-req' key, as
           for these entries, we don't want to 'set' a new gfid */
        if (xdata)
                dict_del (xdata, "gfid-req");

        gf_uuid_copy (tmp_loc.gfid, tmp_gfid);

        /* if revalidate, then we need to have the proper reference */
        if (inode) {
                tmp_loc.inode = inode_ref (inode);
                frame->local = inode_ref (loc->inode);
        } else {
                tmp_loc.inode = inode_ref (loc->inode);
        }

        STACK_WIND (frame, ga_virtual_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &tmp_loc, xdata);

        inode_unref (tmp_loc.inode);

        return 0;

wind:
        /* used for all the normal lookup path */
        STACK_WIND (frame, ga_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);

        return 0;

err:
        STACK_UNWIND_STRICT (lookup, frame, -1, op_errno, loc->inode,
                             &priv->gfiddir_stbuf, xdata,
                             &priv->root_stbuf);
        return 0;
}

int
ga_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
        int op_errno = ENOMEM;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        STACK_WIND (frame, default_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, umask,
                    xdata);

        return 0;

err:
        STACK_UNWIND_STRICT (mkdir, frame, -1, op_errno, loc->inode,
                             NULL, NULL, NULL, xdata);
        return 0;
}


int
ga_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        int op_errno = ENOMEM;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        STACK_WIND (frame, default_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, xdata);

        return 0;

}

int
ga_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
        int op_errno = ENOMEM;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        STACK_WIND (frame, default_symlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                    linkname, loc, umask, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (symlink, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, xdata);

        return 0;
}

int
ga_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata)
{
        int op_errno = ENOMEM;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        STACK_WIND (frame, default_mknod_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev,
                    umask, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, xdata);

        return 0;
}

int
ga_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flag,
          dict_t *xdata)
{
        int   op_errno = ENOMEM;
        int   ret      = -1;
        loc_t ga_loc   = {0, };

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, default_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    &ga_loc, flag, xdata);

        loc_wipe (&ga_loc);
        return 0;
err:
        STACK_UNWIND_STRICT (rmdir, frame, -1, op_errno, NULL,
                             NULL, xdata);

        return 0;
}

int
ga_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
           dict_t *xdata)
{
        int   op_errno = ENOMEM;
        int   ret      = -1;
        loc_t ga_loc   = {0, };

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, default_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    &ga_loc, xflag, xdata);

        loc_wipe (&ga_loc);
        return 0;
err:
        STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL,
                             NULL, xdata);

        return 0;
}

int
ga_rename (call_frame_t *frame, xlator_t *this,
           loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int   op_errno  = ENOMEM;
        int   ret       = 0;
        loc_t ga_oldloc = {0, };
        loc_t ga_newloc = {0, };

        GFID_ACCESS_ENTRY_OP_CHECK (oldloc, op_errno, err);
        GFID_ACCESS_ENTRY_OP_CHECK (newloc, op_errno, err);

        ret = ga_valid_inode_loc_copy (&ga_oldloc, oldloc, this);
        if (ret < 0)
                goto err;

        ret = ga_valid_inode_loc_copy (&ga_newloc, newloc, this);
        if (ret < 0) {
                loc_wipe (&ga_oldloc);
                goto err;
        }

        STACK_WIND (frame, default_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    &ga_oldloc, &ga_newloc, xdata);

        loc_wipe (&ga_newloc);
        loc_wipe (&ga_oldloc);
        return 0;
err:
        STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, xdata);

        return 0;
}


int
ga_link (call_frame_t *frame, xlator_t *this,
         loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int   op_errno  = ENOMEM;
        int   ret       = 0;
        loc_t ga_oldloc = {0, };
        loc_t ga_newloc = {0, };

        GFID_ACCESS_ENTRY_OP_CHECK (oldloc, op_errno, err);
        GFID_ACCESS_ENTRY_OP_CHECK (newloc, op_errno, err);

        ret = ga_valid_inode_loc_copy (&ga_oldloc, oldloc, this);
        if (ret < 0)
                goto err;

        ret = ga_valid_inode_loc_copy (&ga_newloc, newloc, this);
        if (ret < 0) {
                loc_wipe (&ga_oldloc);
                goto err;
        }

        STACK_WIND (frame, default_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    &ga_oldloc, &ga_newloc, xdata);

        loc_wipe (&ga_newloc);
        loc_wipe (&ga_oldloc);
        return 0;

err:
        STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, xdata);

        return 0;
}

int32_t
ga_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc,
            fd_t *fd, dict_t *xdata)
{
        int op_errno = ENOMEM;

        GFID_ACCESS_INODE_OP_CHECK (loc, op_errno, err);

        /* also check if the loc->inode itself is virtual
           inode, if yes, return with failure, mainly because we
           can't handle all the readdirp and other things on it. */
        if (inode_ctx_get (loc->inode, this, NULL) == 0) {
                op_errno = ENOTSUP;
                goto err;
        }

        STACK_WIND (frame, default_opendir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->opendir,
                    loc, fd, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (opendir, frame, -1, op_errno, NULL, xdata);

        return 0;
}

int32_t
ga_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             const char *name, dict_t *xdata)
{
        int   op_errno = ENOMEM;
        int   ret      = -1;
        loc_t ga_loc   = {0, };

        GFID_ACCESS_INODE_OP_CHECK (loc, op_errno, err);
        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, &ga_loc, name, xdata);

        loc_wipe (&ga_loc);

        return 0;
err:
        STACK_UNWIND_STRICT (getxattr, frame, -1, op_errno, NULL, xdata);

        return 0;
}

int32_t
ga_stat (call_frame_t *frame, xlator_t *this, loc_t *loc,
         dict_t *xdata)
{
        int          op_errno = ENOMEM;
        int          ret      = -1;
        loc_t        ga_loc   = {0, };
        ga_private_t *priv    = NULL;

        priv = this->private;
        /* If stat is on ".gfid" itself, do not wind further,
         * return fake stat and return success.
         */
        if (__is_gfid_access_dir(loc->gfid))
                goto out;

        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, default_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, &ga_loc, xdata);

        loc_wipe (&ga_loc);
        return 0;

err:
        STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL, xdata);

        return 0;

out:
        STACK_UNWIND_STRICT (stat, frame, 0, 0, &priv->gfiddir_stbuf, xdata);
        return 0;
}

int32_t
ga_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid,
            dict_t *xdata)
{
        int   op_errno = ENOMEM;
        int   ret      = -1;
        loc_t ga_loc   = {0, };

        GFID_ACCESS_INODE_OP_CHECK (loc, op_errno, err);
        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, default_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, &ga_loc, stbuf, valid,
                    xdata);

        loc_wipe (&ga_loc);
        return 0;
err:
        STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL, xdata);

        return 0;
}

int32_t
ga_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        int   op_errno = ENOMEM;
        int   ret      = -1;
        loc_t ga_loc   = {0, };

        GFID_ACCESS_INODE_OP_CHECK (loc, op_errno, err);
        ret = ga_valid_inode_loc_copy (&ga_loc, loc, this);
        if (ret < 0)
                goto err;

        STACK_WIND (frame, default_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, &ga_loc, name,
                    xdata);

        loc_wipe (&ga_loc);
        return 0;

err:
        STACK_UNWIND_STRICT (removexattr, frame, -1, op_errno, xdata);

        return 0;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_gfid_access_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        ga_private_t *priv = NULL;
        int ret = -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "not configured with exactly one child. exiting");
                goto out;
        }

        /* This can be the top of graph in certain cases */
        if (!this->parents) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dangling volume. check volfile ");
        }

        /* TODO: define a mem-type structure */
        priv = GF_CALLOC (1, sizeof (*priv), gf_gfid_access_mt_priv_t);
        if (!priv)
                goto out;

        priv->newfile_args_pool = mem_pool_new (ga_newfile_args_t, 512);
        if (!priv->newfile_args_pool)
                goto out;

        priv->heal_args_pool = mem_pool_new (ga_heal_args_t, 512);
        if (!priv->heal_args_pool)
                goto out;

        this->local_pool = mem_pool_new (ga_local_t, 16);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto out;
        }

        this->private = priv;

        ret = 0;
out:
        if (ret && priv) {
                if (priv->newfile_args_pool)
                        mem_pool_destroy (priv->newfile_args_pool);
                GF_FREE (priv);
        }

        return ret;
}

void
fini (xlator_t *this)
{
        ga_private_t *priv = NULL;
        priv = this->private;
        this->private = NULL;

        if (priv) {
                if (priv->newfile_args_pool)
                        mem_pool_destroy (priv->newfile_args_pool);
                if (priv->heal_args_pool)
                        mem_pool_destroy (priv->heal_args_pool);
                GF_FREE (priv);
        }

        return;
}

int32_t
ga_dump_inodectx (xlator_t *this, inode_t *inode)
{
        int       ret = -1;
        uint64_t  value = 0;
        inode_t  *tmp_inode = NULL;
        char      key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };

        ret = inode_ctx_get (inode, this, &value);
        if (ret == 0) {
                tmp_inode = (void*) value;
                gf_proc_dump_build_key (key_prefix, this->name, "inode");
                gf_proc_dump_add_section (key_prefix);
                gf_proc_dump_write ("real-gfid", "%s",
                                    uuid_utoa (tmp_inode->gfid));
        }

        return 0;
}

struct xlator_fops fops = {
        .lookup = ga_lookup,

        /* entry fops */
        .mkdir   = ga_mkdir,
        .mknod   = ga_mknod,
        .create  = ga_create,
        .symlink = ga_symlink,
        .link    = ga_link,
        .unlink  = ga_unlink,
        .rmdir   = ga_rmdir,
        .rename  = ga_rename,

        /* handle any other directory operations here */
        .opendir  = ga_opendir,
        .stat     = ga_stat,
        .setattr  = ga_setattr,
        .getxattr = ga_getxattr,
        .removexattr = ga_removexattr,

        /* special fop to handle more entry creations */
        .setxattr = ga_setxattr,
};

struct xlator_cbks cbks = {
        .forget = ga_forget,
};

struct xlator_dumpops dumpops = {
        .inodectx       = ga_dump_inodectx,
};

struct volume_options options[] = {
        /* This translator doesn't take any options, or provide any options */
        { .key  = {NULL} },
};
