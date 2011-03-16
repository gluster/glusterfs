/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __MEM_TYPES_H__
#define __MEM_TYPES_H__


enum gf_common_mem_types_ {
        gf_common_mt_call_stub_t          = 0,
        gf_common_mt_dnscache6            = 1,
        gf_common_mt_data_pair_t          = 2,
        gf_common_mt_data_t               = 3,
        gf_common_mt_dict_t               = 4,
        gf_common_mt_event_pool           = 5,
        gf_common_mt_reg                  = 6,
        gf_common_mt_pollfd               = 7,
        gf_common_mt_epoll_event          = 8,
        gf_common_mt_fdentry_t            = 9,
        gf_common_mt_fdtable_t            = 10,
        gf_common_mt_fd_t                 = 11,
        gf_common_mt_fd_ctx               = 12,
        gf_common_mt_gf_dirent_t          = 13,
        gf_common_mt_glusterfs_ctx_t      = 14,
        gf_common_mt_dentry_t             = 15,
        gf_common_mt_inode_t              = 16,
        gf_common_mt_inode_ctx            = 17,
        gf_common_mt_list_head            = 18,
        gf_common_mt_inode_table_t        = 19,
        gf_common_mt_xlator_t             = 20,
        gf_common_mt_xlator_list_t        = 21,
        gf_common_mt_log_msg              = 22,
        gf_common_mt_client_log           = 23,
        gf_common_mt_volume_opt_list_t    = 24,
        gf_common_mt_gf_hdr_common_t      = 25,
        gf_common_mt_call_frame_t         = 26,
        gf_common_mt_call_stack_t         = 27,
        gf_common_mt_gf_timer_t           = 28,
        gf_common_mt_gf_timer_registry_t  = 29,
        gf_common_mt_transport            = 30,
        gf_common_mt_transport_msg        = 31,
        gf_common_mt_auth_handle_t        = 32,
        gf_common_mt_iobuf                = 33,
        gf_common_mt_iobuf_arena          = 34,
        gf_common_mt_iobref               = 35,
        gf_common_mt_iobuf_pool           = 36,
        gf_common_mt_iovec                = 37,
        gf_common_mt_memdup               = 38,
        gf_common_mt_asprintf             = 39,
        gf_common_mt_strdup               = 40,
        gf_common_mt_socket_private_t     = 41,
        gf_common_mt_ioq                  = 42,
        gf_common_mt_transport_t          = 43,
        gf_common_mt_socket_local_t       = 44,
        gf_common_mt_char                 = 45,
        gf_common_mt_rbthash_table_t      = 46,
        gf_common_mt_rbthash_bucket       = 47,
        gf_common_mt_mem_pool             = 48,
        gf_common_mt_long                 = 49,
        gf_common_mt_rpcsvc_auth_list     = 50,
        gf_common_mt_rpcsvc_t             = 51,
        gf_common_mt_rpcsvc_conn_t        = 52,
        gf_common_mt_rpcsvc_program_t     = 53,
        gf_common_mt_rpcsvc_listener_t    = 54,
        gf_common_mt_rpcsvc_wrapper_t     = 55,
        gf_common_mt_rpcsvc_stage_t       = 56,
        gf_common_mt_rpcclnt_t            = 57,
        gf_common_mt_rpcclnt_savedframe_t = 58,
        gf_common_mt_rpc_trans_t          = 59,
        gf_common_mt_rpc_trans_pollin_t   = 60,
        gf_common_mt_rpc_trans_handover_t = 61,
        gf_common_mt_rpc_trans_reqinfo_t  = 62,
        gf_common_mt_rpc_trans_rsp_t      = 63,
        gf_common_mt_glusterfs_graph_t    = 64,
        gf_common_mt_rdma_private_t       = 65,
        gf_common_mt_rdma_ioq_t           = 66,
        gf_common_mt_rpc_transport_t      = 67,
        gf_common_mt_rdma_local_t         = 68,
        gf_common_mt_rdma_post_t          = 69,
        gf_common_mt_qpent                = 70,
        gf_common_mt_rdma_device_t        = 71,
        gf_common_mt_rdma_context_t       = 72,
        gf_common_mt_sge                  = 73,
        gf_common_mt_rpcclnt_cb_program_t = 74,
        gf_common_mt_libxl_marker_local   = 75,
        gf_common_mt_end                  = 76
};
#endif
