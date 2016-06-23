/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _FDL_H_
#define _FDL_H_

#define NEW_REQUEST     (uint8_t)'N'

typedef struct {
        uint8_t         event_type;     /* e.g. NEW_REQUEST */
        uint8_t         fop_type;       /* e.g. GF_FOP_SETATTR */
        uint16_t        request_id;
        uint32_t        ext_length;
} event_header_t;

enum {
        FDL_IPC_BASE = 0xfeedbee5,       /* ... and they make honey */
        FDL_IPC_CHANGE_TERM,
        FDL_IPC_GET_TERMS,
        FDL_IPC_JBR_SERVER_ROLLBACK
};

#endif /* _FDL_H_ */
