/*
 * Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

/* NOTE: add members ONLY at the end (just before _MAXVALUE) */
/*
 * OTHER NOTE: fop_enum_to_str and fop_enum_to_pri_str (in common-utils.h) also
 * contain lists of fops, so if you update this list UPDATE THOSE TOO.
 */
enum glusterfs_fop_t {
        GF_FOP_NULL = 0,
        GF_FOP_STAT,
        GF_FOP_READLINK,
        GF_FOP_MKNOD,
        GF_FOP_MKDIR,
        GF_FOP_UNLINK,
        GF_FOP_RMDIR,
        GF_FOP_SYMLINK,
        GF_FOP_RENAME,
        GF_FOP_LINK,
        GF_FOP_TRUNCATE,
        GF_FOP_OPEN,
        GF_FOP_READ,
        GF_FOP_WRITE,
        GF_FOP_STATFS,
        GF_FOP_FLUSH,
        GF_FOP_FSYNC,      /* 16 */
        GF_FOP_SETXATTR,
        GF_FOP_GETXATTR,
        GF_FOP_REMOVEXATTR,
        GF_FOP_OPENDIR,
        GF_FOP_FSYNCDIR,
        GF_FOP_ACCESS,
        GF_FOP_CREATE,
        GF_FOP_FTRUNCATE,
        GF_FOP_FSTAT,      /* 25 */
        GF_FOP_LK,
        GF_FOP_LOOKUP,
        GF_FOP_READDIR,
        GF_FOP_INODELK,
        GF_FOP_FINODELK,
        GF_FOP_ENTRYLK,
        GF_FOP_FENTRYLK,
        GF_FOP_XATTROP,
        GF_FOP_FXATTROP,
        GF_FOP_FGETXATTR,
        GF_FOP_FSETXATTR,
        GF_FOP_RCHECKSUM,
        GF_FOP_SETATTR,
        GF_FOP_FSETATTR,
        GF_FOP_READDIRP,
        GF_FOP_FORGET,
        GF_FOP_RELEASE,
        GF_FOP_RELEASEDIR,
        GF_FOP_GETSPEC,
        GF_FOP_FREMOVEXATTR,
	GF_FOP_FALLOCATE,
	GF_FOP_DISCARD,
        GF_FOP_ZEROFILL,
        GF_FOP_IPC,
        GF_FOP_SEEK,
        GF_FOP_LEASE,
        GF_FOP_COMPOUND,
        GF_FOP_GETACTIVELK,
        GF_FOP_SETACTIVELK,
        GF_FOP_MAXVALUE
};

/* Note: Removed event GF_EVENT_CHILD_MODIFIED=8, hence
 *to preserve backward compatibiliy, GF_EVENT_TRANSPORT_CLEANUP = 9
 */
enum glusterfs_event_t {
        GF_EVENT_PARENT_UP = 1,
        GF_EVENT_POLLIN,
        GF_EVENT_POLLOUT,
        GF_EVENT_POLLERR,
        GF_EVENT_CHILD_UP,
        GF_EVENT_CHILD_DOWN,
        GF_EVENT_CHILD_CONNECTING,
        GF_EVENT_TRANSPORT_CLEANUP = 9,
        GF_EVENT_TRANSPORT_CONNECTED,
        GF_EVENT_VOLFILE_MODIFIED,
        GF_EVENT_GRAPH_NEW,
        GF_EVENT_TRANSLATOR_INFO,
        GF_EVENT_TRANSLATOR_OP,
        GF_EVENT_AUTH_FAILED,
        GF_EVENT_VOLUME_DEFRAG,
        GF_EVENT_PARENT_DOWN,
        GF_EVENT_VOLUME_BARRIER_OP,
        GF_EVENT_UPCALL,
        GF_EVENT_SCRUB_STATUS,
        GF_EVENT_SOME_DESCENDENT_DOWN,
        GF_EVENT_SCRUB_ONDEMAND,
        GF_EVENT_SOME_DESCENDENT_UP,
        GF_EVENT_MAXVAL
};

/* List of compound fops. Add fops at the end. */
enum glusterfs_compound_fop_t {
        GF_CFOP_NON_PREDEFINED = 0, /* needs single FOP inspection */
        GF_CFOP_XATTROP_WRITEV,
        GF_CFOP_XATTROP_UNLOCK,
        GF_CFOP_PUT, /* create+write+setxattr+fsync+close+rename */
        GF_CFOP_MAXVALUE
};

enum glusterfs_mgmt_t {
        GF_MGMT_NULL = 0,
        GF_MGMT_MAXVALUE
};

enum gf_op_type_t {
        GF_OP_TYPE_NULL = 0,
        GF_OP_TYPE_FOP,
        GF_OP_TYPE_MGMT,
        GF_OP_TYPE_MAX
};

/* NOTE: all the miscellaneous flags used by GlusterFS should be listed here */
enum glusterfs_lk_cmds_t {
        GF_LK_GETLK = 0,
        GF_LK_SETLK,
        GF_LK_SETLKW,
        GF_LK_RESLK_LCK,
        GF_LK_RESLK_LCKW,
        GF_LK_RESLK_UNLCK,
        GF_LK_GETLK_FD
};

enum glusterfs_lk_types_t {
        GF_LK_F_RDLCK = 0,
        GF_LK_F_WRLCK,
        GF_LK_F_UNLCK,
        GF_LK_EOL
};

/* Lease Types */
enum gf_lease_types_t {
        NONE        = 0,
        GF_RD_LEASE = 1,
        GF_RW_LEASE = 2,
        GF_LEASE_MAX_TYPE
};

/* Lease cmds */
enum gf_lease_cmds_t {
        GF_GET_LEASE = 1,
        GF_SET_LEASE = 2,
        GF_UNLK_LEASE = 3
};

%#define LEASE_ID_SIZE 16 /* 128bits */
struct gf_lease {
        gf_lease_cmds_t  cmd;
        gf_lease_types_t lease_type;
        char             lease_id[LEASE_ID_SIZE];
        unsigned int     lease_flags;
};

enum glusterfs_lk_recovery_cmds_t {
        F_RESLK_LCK = 200,
        F_RESLK_LCKW,
        F_RESLK_UNLCK,
        F_GETLK_FD
};

enum gf_lk_domain_t {
        GF_LOCK_POSIX,
        GF_LOCK_INTERNAL
};

enum entrylk_cmd {
        ENTRYLK_LOCK,
        ENTRYLK_UNLOCK,
        ENTRYLK_LOCK_NB
};

enum entrylk_type {
        ENTRYLK_RDLCK,
        ENTRYLK_WRLCK
};

%#define GF_MAX_LOCK_OWNER_LEN 1024 /* 1kB as per NLM */

/* 16strings-16strings-... */
%#define GF_LKOWNER_BUF_SIZE  ((GF_MAX_LOCK_OWNER_LEN * 2) + (GF_MAX_LOCK_OWNER_LEN / 8))

struct gf_lkowner_t {
        int  len;
        char data[GF_MAX_LOCK_OWNER_LEN];
};

enum gf_xattrop_flags_t {
        GF_XATTROP_ADD_ARRAY,
        GF_XATTROP_ADD_ARRAY64,
        GF_XATTROP_OR_ARRAY,
        GF_XATTROP_AND_ARRAY,
        GF_XATTROP_GET_AND_SET,
        GF_XATTROP_ADD_ARRAY_WITH_DEFAULT,
        GF_XATTROP_ADD_ARRAY64_WITH_DEFAULT
};

enum gf_seek_what_t {
        GF_SEEK_DATA,
        GF_SEEK_HOLE
};

enum gf_upcall_flags_t {
        GF_UPCALL_NULL,
        GF_UPCALL,
        GF_UPCALL_CI_STAT,
        GF_UPCALL_CI_XATTR,
        GF_UPCALL_CI_RENAME,
        GF_UPCALL_CI_NLINK,
        GF_UPCALL_CI_FORGET,
        GF_UPCALL_LEASE_RECALL,
        GF_UPCALL_FLAGS_MAXVALUE
};
