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

/* Macro to define a range of messages for a component. The first argument is
 * the name of the component. The second argument is the number of segments
 * to allocate. The defined values will be GLFS_MSGID_COMP_<name> and
 * GLFS_MSGID_COMP_<name>_END. */
#define GLFS_MSGID_COMP(_name, _blocks) \
        GLFS_MSGID_COMP_##_name, \
        GLFS_MSGID_COMP_##_name##_END = (GLFS_MSGID_COMP_##_name + \
                                         (GLFS_MSGID_SEGMENT * (_blocks)) - 1)

#define GLFS_MSGID(_name, _msgs...) \
        enum _msgid_table_##_name { \
                GLFS_##_name##_COMP_BASE = GLFS_MSGID_COMP_##_name, ## _msgs, \
                GLGS_##_name##_COMP_END \
        }

/* Per module message segments allocated */
/* NOTE: For any new module add to the end the modules */
enum _msgid_comp {
        GLFS_MSGID_RESERVED = GLFS_MSGID_BASE - 1,

        GLFS_MSGID_COMP(GLUSTERFSD,       1),
        GLFS_MSGID_COMP(LIBGLUSTERFS,     1),
        GLFS_MSGID_COMP(RPC_LIB,          1),
        GLFS_MSGID_COMP(RPC_TRANS_RDMA,   1),
        GLFS_MSGID_COMP(API,              1),
        GLFS_MSGID_COMP(CLI,              1),
/* glusterd has a lot of messages, taking 2 segments for the same */
        GLFS_MSGID_COMP(GLUSTERD,         2),
        GLFS_MSGID_COMP(AFR,              1),
        GLFS_MSGID_COMP(DHT,              1),
/* there is no component called 'common', however reserving this segment
 * for common actions/errors like dict_{get/set}, memory accounting*/
        GLFS_MSGID_COMP(COMMON,           1),
        GLFS_MSGID_COMP(UPCALL,           1),
        GLFS_MSGID_COMP(NFS,              1),
        GLFS_MSGID_COMP(POSIX,            1),
        GLFS_MSGID_COMP(PC,               1),
        GLFS_MSGID_COMP(PS,               1),
        GLFS_MSGID_COMP(BITROT_STUB,      1),
        GLFS_MSGID_COMP(CHANGELOG,        1),
        GLFS_MSGID_COMP(BITROT_BITD,      1),
        GLFS_MSGID_COMP(RPC_TRANS_SOCKET, 1),
        GLFS_MSGID_COMP(QUOTA,            1),
        GLFS_MSGID_COMP(CTR,              1),
        GLFS_MSGID_COMP(EC,               1),
        GLFS_MSGID_COMP(IO_CACHE,         1),
        GLFS_MSGID_COMP(IO_THREADS,       1),
        GLFS_MSGID_COMP(MD_CACHE,         1),
        GLFS_MSGID_COMP(OPEN_BEHIND,      1),
        GLFS_MSGID_COMP(QUICK_READ,       1),
        GLFS_MSGID_COMP(READ_AHEAD,       1),
        GLFS_MSGID_COMP(READDIR_AHEAD,    1),
        GLFS_MSGID_COMP(SYMLINK_CACHE,    1),
        GLFS_MSGID_COMP(WRITE_BEHIND,     1),
        GLFS_MSGID_COMP(CHANGELOG_LIB,    1),
        GLFS_MSGID_COMP(SHARD,            1),
        GLFS_MSGID_COMP(JBR,              1),
        GLFS_MSGID_COMP(PL,               1),
        GLFS_MSGID_COMP(DC,               1),
        GLFS_MSGID_COMP(LEASES,           1),
        GLFS_MSGID_COMP(INDEX,            1),
        GLFS_MSGID_COMP(POSIX_ACL,        1),
        GLFS_MSGID_COMP(NLC,              1),
        GLFS_MSGID_COMP(SL,               1),
        GLFS_MSGID_COMP(HAM,              1),
        GLFS_MSGID_COMP(SDFS,             1),
        GLFS_MSGID_COMP(QUIESCE,          1),
        GLFS_MSGID_COMP(TA,               1),

/* --- new segments for messages goes above this line --- */

        GLFS_MSGID_END
};

#endif /* !_GLFS_MESSAGE_ID_H_ */
