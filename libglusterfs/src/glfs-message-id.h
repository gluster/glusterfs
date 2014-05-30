/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_MESSAGE_ID_H_
#define _GLFS_MESSAGE_ID_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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

#define GLFS_MSGID_COMP_RPC_TRANSPORT      GLFS_MSGID_COMP_RPC_LIB_END
#define GLFS_MSGID_COMP_RPC_TRANSPORT_END  GLFS_MSGID_COMP_RPC_TRANSPORT + \
                                           GLFS_MSGID_SEGMENT

#define GLFS_MSGID_COMP_API                GLFS_MSGID_COMP_RPC_TRANSPORT_END
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


/* --- new segments for messages goes above this line --- */

#endif /* !_GLFS_MESSAGE_ID_H_ */
