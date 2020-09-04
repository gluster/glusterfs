/*
  Copyright (c) 2008-2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __MEM_TYPES_H__
#define __MEM_TYPES_H__

enum gf_common_mem_types_ {
    gf_common_mt_dnscache6, /* used only in one location */
    gf_common_mt_event_pool,
    gf_common_mt_reg,
    gf_common_mt_pollfd,    /* used only in one location */
    gf_common_mt_fdentry_t, /* used only in one location */
    gf_common_mt_fdtable_t, /* used only in one location */
    gf_common_mt_fd_ctx,    /* used only in one location */
    gf_common_mt_gf_dirent_t,
    gf_common_mt_inode_t,   /* used only in one location */
    gf_common_mt_inode_ctx, /* used only in one location */
    gf_common_mt_list_head,
    gf_common_mt_inode_table_t, /* used only in one location */
    gf_common_mt_xlator_t,
    gf_common_mt_xlator_list_t, /* used only in one location */
    gf_common_mt_volume_opt_list_t,
    gf_common_mt_gf_timer_t,          /* used only in one location */
    gf_common_mt_gf_timer_registry_t, /* used only in one location */
    gf_common_mt_auth_handle_t,       /* used only in one location */
    gf_common_mt_iobuf,               /* used only in one location */
    gf_common_mt_iobuf_arena,         /* used only in one location */
    gf_common_mt_iobref,              /* used only in one location */
    gf_common_mt_iobuf_pool,          /* used only in one location */
    gf_common_mt_iovec,
    gf_common_mt_memdup,   /* used only in one location */
    gf_common_mt_asprintf, /* used only in one location */
    gf_common_mt_strdup,
    gf_common_mt_socket_private_t, /* used only in one location */
    gf_common_mt_ioq,              /* used only in one location */
    gf_common_mt_char,
    gf_common_mt_rbthash_table_t,      /* used only in one location */
    gf_common_mt_rbthash_bucket,       /* used only in one location */
    gf_common_mt_mem_pool,             /* used only in one location */
    gf_common_mt_rpcsvc_auth_list,     /* used only in one location */
    gf_common_mt_rpcsvc_t,             /* used only in one location */
    gf_common_mt_rpcsvc_program_t,     /* used only in one location */
    gf_common_mt_rpcsvc_listener_t,    /* used only in one location */
    gf_common_mt_rpcsvc_wrapper_t,     /* used only in one location */
    gf_common_mt_rpcclnt_t,            /* used only in one location */
    gf_common_mt_rpcclnt_savedframe_t, /* used only in one location */
    gf_common_mt_rpc_trans_t,
    gf_common_mt_rpc_trans_pollin_t,  /* used only in one location */
    gf_common_mt_rpc_trans_reqinfo_t, /* used only in one location */
    gf_common_mt_glusterfs_graph_t,
    gf_common_mt_rdma_private_t,       /* used only in one location */
    gf_common_mt_rpc_transport_t,      /* used only in one location */
    gf_common_mt_rdma_post_t,          /* used only in one location */
    gf_common_mt_qpent,                /* used only in one location */
    gf_common_mt_rdma_device_t,        /* used only in one location */
    gf_common_mt_rdma_arena_mr,        /* used only in one location */
    gf_common_mt_sge,                  /* used only in one location */
    gf_common_mt_rpcclnt_cb_program_t, /* used only in one location */
    gf_common_mt_libxl_marker_local,   /* used only in one location */
    gf_common_mt_graph_buf,            /* used only in one location */
    gf_common_mt_trie_trie,            /* used only in one location */
    gf_common_mt_trie_data,            /* used only in one location */
    gf_common_mt_trie_node,            /* used only in one location */
    gf_common_mt_trie_buf,             /* used only in one location */
    gf_common_mt_run_argv,             /* used only in one location */
    gf_common_mt_run_logbuf,           /* used only in one location */
    gf_common_mt_fd_lk_ctx_t,          /* used only in one location */
    gf_common_mt_fd_lk_ctx_node_t,     /* used only in one location */
    gf_common_mt_buffer_t,             /* used only in one location */
    gf_common_mt_circular_buffer_t,    /* used only in one location */
    gf_common_mt_eh_t,
    gf_common_mt_store_handle_t, /* used only in one location */
    gf_common_mt_store_iter_t,   /* used only in one location */
    gf_common_mt_drc_client_t,   /* used only in one location */
    gf_common_mt_drc_globals_t,  /* used only in one location */
    gf_common_mt_groups_t,
    gf_common_mt_cliententry_t, /* used only in one location */
    gf_common_mt_clienttable_t, /* used only in one location */
    gf_common_mt_client_t,      /* used only in one location */
    gf_common_mt_client_ctx,    /* used only in one location */
    gf_common_mt_auxgids,       /* used only in one location */
    gf_common_mt_syncopctx,     /* used only in one location */
    gf_common_mt_iobrefs,       /* used only in one location */
    gf_common_mt_gsync_status_t,
    gf_common_mt_uuid_t,
    gf_common_mt_mgmt_v3_lock_obj_t, /* used only in one location */
    gf_common_mt_txn_opinfo_obj_t,   /* used only in one location */
    gf_common_mt_strfd_t,            /* used only in one location */
    gf_common_mt_strfd_data_t,       /* used only in one location */
    gf_common_mt_regex_t,            /* used only in one location */
    gf_common_mt_ereg,               /* used only in one location */
    gf_common_mt_wr,                 /* used only in one location */
    gf_common_mt_dnscache,           /* used only in one location */
    gf_common_mt_dnscache_entry,     /* used only in one location */
    gf_common_mt_parser_t,           /* used only in one location */
    gf_common_quota_meta_t,
    gf_common_mt_rbuf_t,  /* used only in one location */
    gf_common_mt_rlist_t, /* used only in one location */
    gf_common_mt_rvec_t,  /* used only in one location */
    /* glusterd can load the nfs-xlator dynamically and needs these two */
    gf_common_mt_nfs_netgroups,   /* used only in one location */
    gf_common_mt_nfs_exports,     /* used only in one location */
    gf_common_mt_gf_brick_spec_t, /* used only in one location */
    gf_common_mt_int,
    gf_common_mt_pointer,
    gf_common_mt_synctask,  /* used only in one location */
    gf_common_mt_syncstack, /* used only in one location */
    gf_common_mt_syncenv,   /* used only in one location */
    gf_common_mt_scan_data, /* used only in one location */
    gf_common_list_node,
    gf_mt_default_args_t,     /* used only in one location */
    gf_mt_default_args_cbk_t, /* used only in one location */
    /*used for compound fops*/
    gf_mt_compound_req_t, /* used only in one location */
    gf_mt_compound_rsp_t, /* used only in one location */
    gf_common_mt_tw_ctx,  /* used only in one location */
    gf_common_mt_tw_timer_list,
    /*lock migration*/
    gf_common_mt_lock_mig,
    /* throttle */
    gf_common_mt_tbf_t,          /* used only in one location */
    gf_common_mt_tbf_bucket_t,   /* used only in one location */
    gf_common_mt_tbf_throttle_t, /* used only in one location */
    gf_common_mt_pthread_t,      /* used only in one location */
    gf_common_ping_local_t,      /* used only in one location */
    gf_common_volfile_t,
    gf_common_mt_mgmt_v3_lock_timer_t, /* used only in one location */
    gf_common_mt_server_cmdline_t,     /* used only in one location */
    gf_common_mt_latency_t,
    gf_common_mt_end
};
#endif
