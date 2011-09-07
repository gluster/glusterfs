/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
dht_linkfile_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, inode_t *inode,
                         struct iatt *stbuf, struct iatt *preparent,
                         struct iatt *postparent)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        local->linkfile.linkfile_cbk (frame, cookie, this, op_ret, op_errno,
                                      inode, stbuf, preparent, postparent);
        return 0;
}


int
dht_linkfile_create (call_frame_t *frame, fop_mknod_cbk_t linkfile_cbk,
                     xlator_t *tovol, xlator_t *fromvol, loc_t *loc)
{
        dht_local_t *local = NULL;
        dict_t      *dict = NULL;
        int          need_unref = 0;
        int          ret = 0;

        local = frame->local;
        local->linkfile.linkfile_cbk = linkfile_cbk;
        local->linkfile.srcvol = tovol;

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

        ret = dict_set_str (dict, "trusted.glusterfs.dht.linkto",
                                   tovol->name);

        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_INFO,
                        "%s: failed to initialize linkfile data",
                        loc->path);
                goto out;
        }

        STACK_WIND (frame, dht_linkfile_create_cbk,
                    fromvol, fromvol->fops->mknod, loc,
                    S_IFREG | DHT_LINKFILE_MODE, 0, dict);

        if (need_unref && dict)
                dict_unref (dict);

        return 0;
out:
        local->linkfile.linkfile_cbk (frame, NULL, frame->this, -1, ENOMEM,
                                      loc->inode, NULL, NULL, NULL);

        if (need_unref && dict)
                dict_unref (dict);

        return 0;
}


int
dht_linkfile_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         struct iatt *preparent, struct iatt *postparent)
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
                    &unlink_local->loc);

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

        ret = dict_get_ptr (xattr, "trusted.glusterfs.dht.linkto", &volname);

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
