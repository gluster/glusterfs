/*
  Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_MESSAGE_ID_H_
#define _GLFS_MESSAGE_ID_H_

/* Base of all message IDs, all message IDs would be
 * greater than this */
#define GLFS_MSGID_BASE         100000

/* Segment size of allocated range. Any component needing more than this
 * segment size should take multiple segments (at times non contiguous,
 * if extensions are being made post the next segment already allocated) */
#define GLFS_MSGID_SEGMENT      1000

/* Per module message segments allocated */
/* NOTE: For any new module add to the end the modules */
#define GLFS_MSGID_COMP_GLUSTERFSD         GLFS_MSGID_BASE
#define GLFS_MSGID_COMP_GLUSTERFSD_END     GLFS_MSGID_COMP_GLUSTERFSD + \
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_LIBGLUSTERFS       GLFS_MSGID_COMP_GLUSTERFSD_END
#define GLFS_MSGID_COMP_LIBGLUSTERFS_END   GLFS_MSGID_COMP_LIBGLUSTERFS + \
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_RPC_LIB            GLFS_MSGID_COMP_LIBGLUSTERFS_END
#define GLFS_MSGID_COMP_RPC_LIB_END        GLFS_MSGID_COMP_RPC_LIB + \
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_RPC_TRANS_RDMA     GLFS_MSGID_COMP_RPC_LIB_END
#define GLFS_MSGID_COMP_RPC_TRANS_RDMA_END (GLFS_MSGID_COMP_RPC_TRANS_RDMA + \
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_API                GLFS_MSGID_COMP_RPC_TRANS_RDMA_END
#define GLFS_MSGID_COMP_API_END            GLFS_MSGID_COMP_API + \
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_CLI                GLFS_MSGID_COMP_API_END
#define GLFS_MSGID_COMP_CLI_END            GLFS_MSGID_COMP_CLI + \
                                           GLFS_MSGID_SEGMENT

/* glusterd has a lot of messages, taking 2 segments for the same */
#define GLFS_MSGID_GLUSTERD                GLFS_MSGID_COMP_CLI_END
#define GLFS_MSGID_GLUSTERD_END            GLFS_MSGID_GLUSTERD + \
                                           GLFS_MSGID_SEGMENT + \
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_AFR                GLFS_MSGID_GLUSTERD_END
#define GLFS_MSGID_COMP_AFR_END            GLFS_MSGID_COMP_AFR +\
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_DHT                GLFS_MSGID_COMP_AFR_END
#define GLFS_MSGID_COMP_DHT_END            GLFS_MSGID_COMP_DHT +\
                                           GLFS_MSGID_SEGMENT


/* there is no component called 'common', however reserving this segment
 * for common actions/errors like dict_{get/set}, memory accounting*/

#define GLFS_MSGID_COMP_COMMON             GLFS_MSGID_COMP_DHT_END
#define GLFS_MSGID_COMP_COMMON_END         (GLFS_MSGID_COMP_COMMON +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_UPCALL             GLFS_MSGID_COMP_COMMON_END
#define GLFS_MSGID_COMP_UPCALL_END         (GLFS_MSGID_COMP_UPCALL +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_NFS                GLFS_MSGID_COMP_UPCALL_END
#define GLFS_MSGID_COMP_NFS_END            (GLFS_MSGID_COMP_NFS +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_POSIX              GLFS_MSGID_COMP_NFS_END
#define GLFS_MSGID_COMP_POSIX_END          (GLFS_MSGID_COMP_POSIX +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_PC                 GLFS_MSGID_COMP_POSIX_END
#define GLFS_MSGID_COMP_PC_END             (GLFS_MSGID_COMP_PC +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_PS                 GLFS_MSGID_COMP_PC_END
#define GLFS_MSGID_COMP_PS_END             (GLFS_MSGID_COMP_PS +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_BITROT_STUB        GLFS_MSGID_COMP_PS_END
#define GLFS_MSGID_COMP_BITROT_STUB_END    (GLFS_MSGID_COMP_BITROT_STUB +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_CHANGELOG          GLFS_MSGID_COMP_BITROT_STUB_END
#define GLFS_MSGID_COMP_CHANGELOG_END      (GLFS_MSGID_COMP_CHANGELOG +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_BITROT_BITD        GLFS_MSGID_COMP_CHANGELOG_END
#define GLFS_MSGID_COMP_BITROT_BITD_END    (GLFS_MSGID_COMP_BITROT_BITD +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_RPC_TRANS_SOCKET        GLFS_MSGID_COMP_BITROT_BITD_END
#define GLFS_MSGID_COMP_RPC_TRANS_SOCKET_END    (GLFS_MSGID_COMP_RPC_TRANS_SOCKET + \
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_QUOTA              GLFS_MSGID_COMP_RPC_TRANS_SOCKET_END
#define GLFS_MSGID_COMP_QUOTA_END          (GLFS_MSGID_COMP_QUOTA +\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_CTR                GLFS_MSGID_COMP_QUOTA_END
#define GLFS_MSGID_COMP_CTR_END            (GLFS_MSGID_COMP_CTR+\
                                           GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_EC               GLFS_MSGID_COMP_CTR_END
#define GLFS_MSGID_COMP_EC_END           (GLFS_MSGID_COMP_EC +\
                                         GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_IO_CACHE                GLFS_MSGID_COMP_EC_END
#define GLFS_MSGID_COMP_IO_CACHE_END            (GLFS_MSGID_COMP_IO_CACHE+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_IO_THREADS              GLFS_MSGID_COMP_IO_CACHE_END
#define GLFS_MSGID_COMP_IO_THREADS_END          (GLFS_MSGID_COMP_IO_THREADS+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_MD_CACHE                GLFS_MSGID_COMP_IO_THREADS_END
#define GLFS_MSGID_COMP_MD_CACHE_END            (GLFS_MSGID_COMP_MD_CACHE+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_OPEN_BEHIND             GLFS_MSGID_COMP_MD_CACHE_END
#define GLFS_MSGID_COMP_OPEN_BEHIND_END         (GLFS_MSGID_COMP_OPEN_BEHIND+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_QUICK_READ              GLFS_MSGID_COMP_OPEN_BEHIND_END
#define GLFS_MSGID_COMP_QUICK_READ_END          (GLFS_MSGID_COMP_QUICK_READ+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_READ_AHEAD              GLFS_MSGID_COMP_QUICK_READ_END
#define GLFS_MSGID_COMP_READ_AHEAD_END          (GLFS_MSGID_COMP_READ_AHEAD+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_READDIR_AHEAD           GLFS_MSGID_COMP_READ_AHEAD_END
#define GLFS_MSGID_COMP_READDIR_AHEAD_END       (GLFS_MSGID_COMP_READDIR_AHEAD+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_SYMLINK_CACHE           \
GLFS_MSGID_COMP_READDIR_AHEAD_END
#define GLFS_MSGID_COMP_SYMLINK_CACHE_END \
(GLFS_MSGID_COMP_SYMLINK_CACHE+ \
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_WRITE_BEHIND             \
GLFS_MSGID_COMP_SYMLINK_CACHE_END
#define GLFS_MSGID_COMP_WRITE_BEHIND_END        (GLFS_MSGID_COMP_WRITE_BEHIND+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_CHANGELOG_LIB           GLFS_MSGID_COMP_WRITE_BEHIND_END
#define GLFS_MSGID_COMP_CHANGELOG_LIB_END       (GLFS_MSGID_COMP_CHANGELOG_LIB+\
                                                GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_SHARD               GLFS_MSGID_COMP_CHANGELOG_LIB_END
#define GLFS_MSGID_COMP_SHARD_END           (GLFS_MSGID_COMP_SHARD +\
                                             GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_JBR                     GLFS_MSGID_COMP_SHARD_END
#define GLFS_MSGID_COMP_JBR_END                 (GLFS_MSGID_COMP_SHARD_END+\
                                                 GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_PL               GLFS_MSGID_COMP_JBR_END
#define GLFS_MSGID_COMP_PL_END           (GLFS_MSGID_COMP_PL +\
                                         GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_DC                     GLFS_MSGID_COMP_PL_END
#define GLFS_MSGID_COMP_DC_END                 (GLFS_MSGID_COMP_PL_END+\
                                                 GLFS_MSGID_SEGMENT)

#define GLFS_MSGID_COMP_LEASES             GLFS_MSGID_COMP_DC_END
#define GLFS_MSGID_COMP_LEASES_END         (GLFS_MSGID_COMP_LEASES +\
                                           GLFS_MSGID_SEGMENT)

/* --- new segments for messages goes above this line --- */

#endif /* !_GLFS_MESSAGE_ID_H_ */
