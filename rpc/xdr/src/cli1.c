/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#include "cli1.h"
#include "xdr-generic.h"

ssize_t
gf_xdr_serialize_cli_probe_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_probe_rsp);

}

ssize_t
gf_xdr_to_cli_probe_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_probe_req);
}

ssize_t
gf_xdr_to_cli_probe_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_probe_rsp);
}

ssize_t
gf_xdr_from_cli_probe_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_probe_req);
}

ssize_t
gf_xdr_serialize_cli_deprobe_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_deprobe_rsp);

}

ssize_t
gf_xdr_to_cli_deprobe_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_deprobe_req);
}

ssize_t
gf_xdr_to_cli_deprobe_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_deprobe_rsp);
}

ssize_t
gf_xdr_from_cli_deprobe_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_deprobe_req);
}

ssize_t
gf_xdr_serialize_cli_peer_list_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_peer_list_rsp);

}

ssize_t
gf_xdr_to_cli_peer_list_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_peer_list_req);
}

ssize_t
gf_xdr_to_cli_peer_list_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
}

ssize_t
gf_xdr_from_cli_peer_list_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_peer_list_req);
}

ssize_t
gf_xdr_serialize_cli_get_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_get_vol_rsp);

}

ssize_t
gf_xdr_to_cli_get_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_get_vol_req);
}

ssize_t
gf_xdr_to_cli_get_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_get_vol_rsp);
}

ssize_t
gf_xdr_from_cli_get_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_get_vol_req);
}
ssize_t
gf_xdr_serialize_cli_create_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_create_vol_rsp);

}

ssize_t
gf_xdr_to_cli_create_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_create_vol_req);
}

ssize_t
gf_xdr_to_cli_create_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_create_vol_rsp);
}

ssize_t
gf_xdr_from_cli_create_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_create_vol_req);
}


ssize_t
gf_xdr_serialize_cli_delete_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_delete_vol_rsp);

}

ssize_t
gf_xdr_to_cli_delete_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_delete_vol_req);
}


ssize_t
gf_xdr_to_cli_delete_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_delete_vol_rsp);
}

ssize_t
gf_xdr_from_cli_delete_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_delete_vol_req);
}

ssize_t
gf_xdr_serialize_cli_start_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_start_vol_rsp);

}

ssize_t
gf_xdr_to_cli_start_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_start_vol_req);
}

ssize_t
gf_xdr_to_cli_start_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_start_vol_rsp);
}

ssize_t
gf_xdr_from_cli_start_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_start_vol_req);
}


ssize_t
gf_xdr_serialize_cli_stop_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_stop_vol_rsp);

}

ssize_t
gf_xdr_to_cli_stop_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_stop_vol_req);
}

ssize_t
gf_xdr_to_cli_stop_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_stop_vol_rsp);
}

ssize_t
gf_xdr_from_cli_stop_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_stop_vol_req);
}


ssize_t
gf_xdr_serialize_cli_rename_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_rename_vol_rsp);

}

ssize_t
gf_xdr_to_cli_rename_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_rename_vol_req);
}

ssize_t
gf_xdr_to_cli_rename_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_rename_vol_rsp);
}

ssize_t
gf_xdr_from_cli_rename_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_rename_vol_req);
}


ssize_t
gf_xdr_serialize_cli_defrag_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_defrag_vol_rsp);

}

ssize_t
gf_xdr_to_cli_defrag_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_defrag_vol_rsp);
}

ssize_t
gf_xdr_to_cli_defrag_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_defrag_vol_req);
}

ssize_t
gf_xdr_from_cli_defrag_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_defrag_vol_req);
}



ssize_t
gf_xdr_serialize_cli_add_brick_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_add_brick_rsp);

}

ssize_t
gf_xdr_to_cli_add_brick_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_add_brick_req);
}

ssize_t
gf_xdr_to_cli_add_brick_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_add_brick_rsp);
}

ssize_t
gf_xdr_from_cli_add_brick_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_add_brick_req);
}


ssize_t
gf_xdr_serialize_cli_remove_brick_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_remove_brick_rsp);

}

ssize_t
gf_xdr_to_cli_remove_brick_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_remove_brick_req);
}


ssize_t
gf_xdr_to_cli_remove_brick_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_remove_brick_rsp);
}

ssize_t
gf_xdr_from_cli_remove_brick_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_remove_brick_req);
}


ssize_t
gf_xdr_serialize_cli_replace_brick_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_replace_brick_rsp);

}

ssize_t
gf_xdr_to_cli_replace_brick_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_replace_brick_req);
}

ssize_t
gf_xdr_to_cli_replace_brick_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_replace_brick_rsp);
}

ssize_t
gf_xdr_from_cli_replace_brick_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_replace_brick_req);
}

ssize_t
gf_xdr_serialize_cli_reset_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_reset_vol_rsp);

}

ssize_t
gf_xdr_to_cli_reset_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_reset_vol_req);
}

ssize_t
gf_xdr_to_cli_reset_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_reset_vol_rsp);
}


ssize_t
gf_xdr_from_cli_reset_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_reset_vol_req);
}

ssize_t
gf_xdr_serialize_cli_gsync_set_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_gsync_set_rsp);

}

ssize_t
gf_xdr_to_cli_gsync_set_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_gsync_set_req);
}

ssize_t
gf_xdr_to_cli_gsync_set_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_gsync_set_rsp);
}


ssize_t
gf_xdr_from_cli_gsync_set_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_gsync_set_req);
}

ssize_t
gf_xdr_serialize_cli_set_vol_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_set_vol_rsp);

}

ssize_t
gf_xdr_to_cli_set_vol_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_set_vol_req);
}

ssize_t
gf_xdr_to_cli_set_vol_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_set_vol_rsp);
}

ssize_t
gf_xdr_from_cli_set_vol_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_set_vol_req);
}

/* log */
ssize_t
gf_xdr_serialize_cli_log_filename_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_log_filename_rsp);

}

ssize_t
gf_xdr_to_cli_log_filename_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_log_filename_req);
}

ssize_t
gf_xdr_to_cli_log_filename_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_log_filename_rsp);
}

ssize_t
gf_xdr_from_cli_log_filename_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_log_filename_req);
}

ssize_t
gf_xdr_serialize_cli_log_locate_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_log_locate_rsp);

}

ssize_t
gf_xdr_to_cli_log_locate_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_log_locate_req);
}

ssize_t
gf_xdr_to_cli_log_locate_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_log_locate_rsp);
}

ssize_t
gf_xdr_from_cli_log_locate_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_log_locate_req);
}

ssize_t
gf_xdr_serialize_cli_log_rotate_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf1_cli_log_rotate_rsp);

}

ssize_t
gf_xdr_to_cli_log_rotate_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_log_rotate_req);
}

ssize_t
gf_xdr_to_cli_log_rotate_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_log_rotate_rsp);
}

ssize_t
gf_xdr_from_cli_log_rotate_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf1_cli_log_rotate_req);
}

ssize_t
gf_xdr_to_cli_sync_volume_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_sync_volume_req);
}

ssize_t
gf_xdr_from_cli_sync_volume_req (struct iovec outmsg, void *args)
{
        return xdr_serialize_generic (outmsg, (void *)args,
                                     (xdrproc_t)xdr_gf1_cli_sync_volume_req);
}

ssize_t
gf_xdr_to_cli_sync_volume_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_sync_volume_rsp);
}

ssize_t
gf_xdr_from_cli_sync_volume_rsp (struct iovec outmsg, void *args)
{
        return xdr_serialize_generic (outmsg, (void *)args,
                                      (xdrproc_t)xdr_gf1_cli_sync_volume_rsp);
}

ssize_t
gf_xdr_to_cli_fsm_log_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_fsm_log_req);
}

ssize_t
gf_xdr_from_cli_fsm_log_req (struct iovec outmsg, void *args)
{
        return xdr_serialize_generic (outmsg, (void *)args,
                                     (xdrproc_t)xdr_gf1_cli_fsm_log_req);
}

ssize_t
gf_xdr_to_cli_fsm_log_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
}

ssize_t
gf_xdr_from_cli_fsm_log_rsp (struct iovec outmsg, void *args)
{
        return xdr_serialize_generic (outmsg, (void *)args,
                                      (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
}
