/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "gfid-access.h"
#include "inode.h"
#include "byte-order.h"



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
        if (len == blob_len)
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
                blob += sizeof (uint32_t);
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
                blob += (len + 1);
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
                blob += sizeof (uint32_t);
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

        parent = loc->inode;
        ret = inode_ctx_get (loc->inode, this, &value);
        if (!ret) {
                parent = (void *)value;
                if (uuid_is_null (parent->gfid))
                        parent = loc->inode;
        }

        /* parent itself should be looked up */
        uuid_copy (new_loc->pargfid, parent->gfid);
        new_loc->parent = inode_ref (parent);

        new_loc->inode = inode_grep (parent->table, parent, bname);
        if (!new_loc->inode)
                new_loc->inode = inode_new (parent->table);

        loc_path (new_loc, bname);
        new_loc->name = basename (new_loc->path);

        /* As GFID would not be set on the entry yet, lets not send entry
           gfid in the request */
        /*uuid_copy (new_loc->gfid, (const unsigned char *)gfid); */

        ret = dict_set_static_bin (xdata, "gfid-req", gfid, 16);
        if (ret < 0)
                goto out;

        ret = 0;

out:
        return ret;
}



static gf_boolean_t
__is_gfid_access_dir (uuid_t gfid)
{
        uuid_t  aux_gfid;

        memset (aux_gfid, 0, 16);
        aux_gfid[15] = GF_AUX_GFID;

        if (uuid_compare (gfid, aux_gfid) == 0)
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

static int32_t
ga_newentry_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                         struct iatt *statpost,
                         dict_t *xdata)
{
        ga_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;

        /* don't worry about inode linking and other stuff. They'll happen on
         * the next lookup.
         */
        STACK_DESTROY (frame->root);

        STACK_UNWIND_STRICT (setxattr, local->orig_frame, op_ret,
                             op_errno, xdata);

        loc_wipe (&local->loc);
        mem_put (local);

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
        struct iatt temp_stat = {0,};

        local = frame->local;

        if (!local->uid && !local->gid)
                goto done;

        temp_stat.ia_uid = local->uid;
        temp_stat.ia_gid = local->gid;

        STACK_WIND (frame, ga_newentry_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, &local->loc, &temp_stat,
                    (GF_SET_ATTR_UID | GF_SET_ATTR_GID), xdata);

        return 0;

done:
        /* don't worry about inode linking and other stuff. They'll happen on
         * the next lookup.
         */
        frame->local = NULL;
        STACK_DESTROY (frame->root);

        STACK_UNWIND_STRICT (setxattr, local->orig_frame, op_ret,
                             op_errno, xdata);

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
        mode_t             mode      = 0;
        ga_local_t        *local     = NULL;
        uuid_t             gfid      = {0,};

        args = ga_newfile_parse_args (this, data);
        if (!args)
                goto out;

        ret = uuid_parse (args->gfid, gfid);
        if (ret)
                goto out;

        if (!xdata)
                xdata = dict_new ();

        ret = ga_fill_tmp_loc (loc, this, gfid,
                               args->bname, xdata, &tmp_loc);
        if (ret)
                goto out;

        new_frame = copy_frame (frame);
        if (!new_frame)
                goto out;

        local = mem_get0 (this->local_pool);
        local->orig_frame = frame;

        local->uid = args->uid;
        local->gid = args->gid;

        loc_copy (&local->loc, &tmp_loc);

        new_frame->local = local;

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
                mode = (S_IFMT & args->st_mode) |
                        (07777 & args->args.mknod.mode);;

                STACK_WIND (new_frame, ga_newentry_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                            &tmp_loc, mode,
                            args->args.mknod.rdev, args->args.mknod.umask,
                            xdata);
        }

        ret = 0;
out:
        ga_newfile_args_free (args);

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

        ret = uuid_parse (args->gfid, gfid);
        if (ret)
                goto out;

        if (!xdata)
                xdata = dict_new ();

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
        inode_t *unref    = NULL;

        if ((loc->name && !strcmp (GF_GFID_DIR, loc->name)) &&
            ((loc->parent &&
              __is_root_gfid (loc->parent->gfid)) ||
             __is_root_gfid (loc->pargfid))) {
                op_errno = EPERM;
                goto err;
        }

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
        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, ga_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
                    xdata);
        if (unref)
                inode_unref (unref);

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
        int       j           = 0;
        int       i           = 0;
        int       ret         = 0;
        uint64_t  temp_ino    = 0;
        inode_t  *cbk_inode   = NULL;
        inode_t  *true_inode  = NULL;
        uuid_t    random_gfid = {0,};

        if (frame->local)
                cbk_inode = frame->local;
        else
                cbk_inode = inode;

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
                        cbk_inode = inode_new (inode->table);

                        if (!cbk_inode) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unwind;
                        }
                        /* the inode is not present in itable, ie, the actual
                           path is not yet looked up. Use the current inode
                           itself for now */

                        inode_link (inode, NULL, NULL, buf);
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

        if (!uuid_is_null (cbk_inode->gfid)) {
                /* if the previous linked inode is used, use the
                   same gfid */
                uuid_copy (random_gfid, cbk_inode->gfid);
        } else {
                /* replace the buf->ia_gfid to a random gfid
                   for directory, for files, what we received is fine */
                uuid_generate (random_gfid);
        }

        uuid_copy (buf->ia_gfid, random_gfid);

        for (i = 15; i > (15 - 8); i--) {
                temp_ino += (uint64_t)(buf->ia_gfid[i]) << j;
                j += 8;
        }
        buf->ia_ino = temp_ino;

unwind:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, cbk_inode, buf,
                             xdata, postparent);

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
                        uuid_copy (tmp_loc.gfid, loc->inode->gfid);

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

        priv = this->private;

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
        ret = uuid_parse (loc->name, tmp_gfid);
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
                goto err;
        }

discover:
        /* for the virtual entries, we don't need to send 'gfid-req' key, as
           for these entries, we don't want to 'set' a new gfid */
        if (xdata)
                dict_del (xdata, "gfid-req");

        uuid_copy (tmp_loc.gfid, tmp_gfid);

        /* if revalidate, then we need to have the proper reference */
        if (inode) {
                tmp_loc.inode = inode_ref (inode);
                frame->local = loc->inode;
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
        int op_errno = 0;

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
        int op_errno = 0;

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
        int op_errno = 0;

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
        int op_errno = 0;

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
        int op_errno = 0;
        inode_t *unref = NULL;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, default_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    loc, flag, xdata);
        if (unref)
                inode_unref (unref);

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
        int op_errno = 0;
        inode_t *unref = NULL;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, default_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);

        if (unref)
                inode_unref (unref);

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
        int op_errno = 0;
        inode_t *oldloc_unref = NULL;
        inode_t *newloc_unref = NULL;

        GFID_ACCESS_ENTRY_OP_CHECK (oldloc, op_errno, err);
        GFID_ACCESS_ENTRY_OP_CHECK (newloc, op_errno, err);

        GFID_ACCESS_GET_VALID_DIR_INODE (this, oldloc, oldloc_unref,
                                         handle_newloc);

handle_newloc:
        GFID_ACCESS_GET_VALID_DIR_INODE (this, newloc, newloc_unref, wind);

wind:
        STACK_WIND (frame, default_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);

        if (oldloc_unref)
                inode_unref (oldloc_unref);

        if (newloc_unref)
                inode_unref (newloc_unref);

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
        int op_errno = 0;
        inode_t *oldloc_unref = NULL;
        inode_t *newloc_unref = NULL;

        GFID_ACCESS_ENTRY_OP_CHECK (oldloc, op_errno, err);
        GFID_ACCESS_ENTRY_OP_CHECK (newloc, op_errno, err);

        GFID_ACCESS_GET_VALID_DIR_INODE (this, oldloc, oldloc_unref,
                                         handle_newloc);

handle_newloc:
        GFID_ACCESS_GET_VALID_DIR_INODE (this, newloc, newloc_unref, wind);

wind:
        STACK_WIND (frame, default_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);

        if (oldloc_unref)
                inode_unref (oldloc_unref);

        if (newloc_unref)
                inode_unref (newloc_unref);

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
        int op_errno = 0;

        GFID_ACCESS_ENTRY_OP_CHECK (loc, op_errno, err);

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
        inode_t *unref = NULL;

        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);

        if (unref)
                inode_unref (unref);

        return 0;
}

int32_t
ga_stat (call_frame_t *frame, xlator_t *this, loc_t *loc,
         dict_t *xdata)
{
        inode_t *unref = NULL;

        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, default_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);
        if (unref)
                inode_unref (unref);

        return 0;
}

int32_t
ga_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid,
            dict_t *xdata)
{
        inode_t *unref = NULL;

        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, default_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid,
                    xdata);
        if (unref)
                inode_unref (unref);

        return 0;
}

int32_t
ga_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        inode_t *unref = NULL;

        GFID_ACCESS_GET_VALID_DIR_INODE (this, loc, unref, wind);

wind:
        STACK_WIND (frame, default_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name,
                    xdata);
        if (unref)
                inode_unref (unref);

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

struct volume_options options[] = {
        /* This translator doesn't take any options, or provide any options */
        { .key  = {NULL} },
};
