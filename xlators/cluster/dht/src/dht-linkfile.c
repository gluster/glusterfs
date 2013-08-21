/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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


#include "glusterfs.h"
#include "xlator.h"
#include "compat.h"
#include "dht-common.h"

int
dht_linkfile_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno,
                         inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                         struct iatt *postparent)
{
        char          is_linkfile   = 0;
        dht_conf_t   *conf          = NULL;
        dht_local_t  *local         = NULL;
        call_frame_t *prev          = NULL;

        local = frame->local;
        prev = cookie;
        conf = this->private;

        if (op_ret)
                goto out;

        is_linkfile = check_is_linkfile (inode, stbuf, xattr,
                                         conf->link_xattr_name);
        if (!is_linkfile)
                gf_log (this->name, GF_LOG_WARNING, "got non-linkfile %s:%s",
                        prev->this->name, local->loc.path);
out:
        local->linkfile.linkfile_cbk (frame, cookie, this, op_ret, op_errno,
                                      inode, stbuf, postparent, postparent,
                                      xattr);
        return 0;
}

#define is_equal(a, b) (a == b)
int
dht_linkfile_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, inode_t *inode,
                         struct iatt *stbuf, struct iatt *preparent,
                         struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *subvol = NULL;
        call_frame_t *prev = NULL;
        dict_t       *xattrs = NULL;
        dht_conf_t   *conf = NULL;
        int           ret = -1;

        local = frame->local;

        if (!op_ret)
                local->linked = _gf_true;

        FRAME_SU_UNDO (frame, dht_local_t);

        if (op_ret && (op_errno == EEXIST)) {
                conf = this->private;
                prev = cookie;
                subvol = prev->this;
                if (!subvol)
                        goto out;
                xattrs = dict_new ();
                if (!xattrs)
                        goto out;
                ret = dict_set_uint32 (xattrs, conf->link_xattr_name, 256);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set linkto key");
                        goto out;
                }

                STACK_WIND (frame, dht_linkfile_lookup_cbk, subvol,
                            subvol->fops->lookup, &local->loc, xattrs);
                if (xattrs)
                        dict_unref (xattrs);
                return 0;
        }
out:
        local->linkfile.linkfile_cbk (frame, cookie, this, op_ret, op_errno,
                                      inode, stbuf, preparent, postparent,
                                      xdata);
        if (xattrs)
                dict_unref (xattrs);
        return 0;
}


int
dht_linkfile_create (call_frame_t *frame, fop_mknod_cbk_t linkfile_cbk,
                     xlator_t *this,
                     xlator_t *tovol, xlator_t *fromvol, loc_t *loc)
{
        dht_local_t *local = NULL;
        dict_t      *dict = NULL;
        int          need_unref = 0;
        int          ret = 0;
        dht_conf_t  *conf = this->private;

        local = frame->local;
        local->linkfile.linkfile_cbk = linkfile_cbk;
        local->linkfile.srcvol = tovol;

        local->linked = _gf_false;

        dict = local->params;
        if (!dict) {
                dict = dict_new ();
                if (!dict)
                        goto out;
                need_unref = 1;
        }

        if (!uuid_is_null (local->gfid)) {
                ret = dict_set_static_bin (dict, "gfid-req", local->gfid, 16);
                if (ret)
                        gf_log ("dht-linkfile", GF_LOG_INFO,
                                "%s: gfid set failed", loc->path);
        }

        ret = dict_set_str (dict, GLUSTERFS_INTERNAL_FOP_KEY, "yes");
        if (ret)
                gf_log ("dht-linkfile", GF_LOG_INFO,
                        "%s: internal-fop set failed", loc->path);

        ret = dict_set_str (dict, conf->link_xattr_name, tovol->name);

        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_INFO,
                        "%s: failed to initialize linkfile data",
                        loc->path);
                goto out;
        }

        local->link_subvol = fromvol;
        /* Always create as root:root. dht_linkfile_attr_heal fixes the
         * ownsership */
        FRAME_SU_DO (frame, dht_local_t);
        STACK_WIND (frame, dht_linkfile_create_cbk,
                    fromvol, fromvol->fops->mknod, loc,
                    S_IFREG | DHT_LINKFILE_MODE, 0, 0, dict);

        if (need_unref && dict)
                dict_unref (dict);

        return 0;
out:
        local->linkfile.linkfile_cbk (frame, NULL, frame->this, -1, ENOMEM,
                                      loc->inode, NULL, NULL, NULL, NULL);

        if (need_unref && dict)
                dict_unref (dict);

        return 0;
}


int
dht_linkfile_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         struct iatt *preparent, struct iatt *postparent,
                         dict_t *xdata)
{
        dht_local_t   *local = NULL;
        call_frame_t  *prev = NULL;
        xlator_t      *subvol = NULL;

        local = frame->local;
        prev = cookie;
        subvol = prev->this;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "unlinking linkfile %s on %s failed (%s)",
                        local->loc.path, subvol->name, strerror (op_errno));
        }

        DHT_STACK_DESTROY (frame);

        return 0;
}


int
dht_linkfile_unlink (call_frame_t *frame, xlator_t *this,
                     xlator_t *subvol, loc_t *loc)
{
        call_frame_t *unlink_frame = NULL;
        dht_local_t  *unlink_local = NULL;

        unlink_frame = copy_frame (frame);
        if (!unlink_frame) {
                goto err;
        }

        /* Using non-fop value here, as anyways, 'local->fop' is not used in
           this particular case */
        unlink_local = dht_local_init (unlink_frame, loc, NULL,
                                       GF_FOP_MAXVALUE);
        if (!unlink_local) {
                goto err;
        }

        STACK_WIND (unlink_frame, dht_linkfile_unlink_cbk,
                    subvol, subvol->fops->unlink,
                    &unlink_local->loc, 0, NULL);

        return 0;
err:
        if (unlink_frame)
                DHT_STACK_DESTROY (unlink_frame);

        return -1;
}


xlator_t *
dht_linkfile_subvol (xlator_t *this, inode_t *inode, struct iatt *stbuf,
                     dict_t *xattr)
{
        dht_conf_t *conf = NULL;
        xlator_t   *subvol = NULL;
        void       *volname = NULL;
        int         i = 0, ret = 0;

        conf = this->private;

        if (!xattr)
                goto out;

        ret = dict_get_ptr (xattr, conf->link_xattr_name, &volname);

        if ((-1 == ret) || !volname)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (strcmp (conf->subvolumes[i]->name, (char *)volname) == 0) {
                        subvol = conf->subvolumes[i];
                        break;
                }
        }

out:
        return subvol;
}

int
dht_linkfile_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int op_ret, int op_errno, struct iatt *statpre,
                          struct iatt *statpost, dict_t *xdata)
{
        dht_local_t *local = NULL;
        loc_t *loc = NULL;

        local = frame->local;
        loc = &local->loc;

        if (op_ret)
                gf_log (this->name, GF_LOG_ERROR, "setattr of uid/gid on %s"
                        " :<gfid:%s> failed (%s)",
                        (loc->path? loc->path: "NULL"),
                        uuid_utoa(local->gfid), strerror(op_errno));

        DHT_STACK_DESTROY (frame);

       return 0;
}

int
dht_linkfile_attr_heal (call_frame_t *frame, xlator_t *this)
{
        int     ret = -1;
        call_frame_t *copy = NULL;
        dht_local_t  *local = NULL;
        dht_local_t  *copy_local = NULL;
        xlator_t     *subvol = NULL;
        struct iatt   stbuf = {0,};

        local = frame->local;

        GF_VALIDATE_OR_GOTO ("dht", local, out);
        GF_VALIDATE_OR_GOTO ("dht", local->link_subvol, out);

        if (local->stbuf.ia_type == IA_INVAL)
                return 0;

        uuid_copy (local->loc.gfid, local->stbuf.ia_gfid);

        copy = copy_frame (frame);

        if (!copy)
                goto out;

       copy_local = dht_local_init (copy, &local->loc, NULL, 0);

       if (!copy_local)
               goto out;

        stbuf = local->stbuf;
        subvol = local->link_subvol;

        copy->local = copy_local;

        FRAME_SU_DO (copy, dht_local_t);

        STACK_WIND (copy, dht_linkfile_setattr_cbk, subvol,
                    subvol->fops->setattr, &copy_local->loc,
                    &stbuf, (GF_SET_ATTR_UID | GF_SET_ATTR_GID), NULL);
        ret = 0;
out:
        return ret;
}
