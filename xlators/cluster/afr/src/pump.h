/*
   Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __PUMP_H__
#define __PUMP_H__

#include "syncop.h"

/* FIXME: Needs to be defined in a common file */
#define CLIENT_CMD_CONNECT "trusted.glusterfs.client-connect"
#define CLIENT_CMD_DISCONNECT "trusted.glusterfs.client-disconnect"

#define PUMP_CMD_START  "trusted.glusterfs.pump.start"
#define PUMP_CMD_COMMIT "trusted.glusterfs.pump.commit"
#define PUMP_CMD_ABORT  "trusted.glusterfs.pump.abort"
#define PUMP_CMD_PAUSE  "trusted.glusterfs.pump.pause"
#define PUMP_CMD_STATUS "trusted.glusterfs.pump.status"

#define PUMP_SOURCE_COMPLETE "trusted.glusterfs.pump-source-complete"
#define PUMP_SINK_COMPLETE "trusted.glusterfs.pump-sink-complete"

#define PUMP_PATH "trusted.glusterfs.pump-path"

#define PUMP_SOURCE_CHILD(xl) (xl->children->xlator)
#define PUMP_SINK_CHILD(xl) (xl->children->next->xlator)

typedef enum {
        PUMP_STATE_RUNNING,             /* Pump is running and migrating files */
        PUMP_STATE_RESUME,              /* Pump is resuming from a previous pause */
        PUMP_STATE_PAUSE,               /* Pump is paused */
        PUMP_STATE_ABORT,               /* Pump is aborted */
        PUMP_STATE_COMMIT,              /* Pump is commited */
} pump_state_t;

typedef struct _pump_private {
	struct syncenv *env;            /* The env pointer to the pump synctask */
        char *resume_path;              /* path to resume from the last pause */
        gf_lock_t resume_path_lock;     /* Synchronize resume_path changes */
        gf_lock_t pump_state_lock;      /* Synchronize pump_state changes */
        pump_state_t pump_state;        /* State of pump */
        char current_file[PATH_MAX];    /* Current file being pumped */
        uint64_t number_files_pumped;   /* Number of files pumped */
        gf_boolean_t pump_finished;     /* Boolean to indicate pump termination */
        char pump_start_pending;        /* Boolean to mark start pending until
                                           CHILD_UP */
        call_stub_t *cleaner;
} pump_private_t;

void
build_root_loc (inode_t *inode, loc_t *loc);
int pump_start (call_frame_t *frame, xlator_t *this);

gf_boolean_t
pump_command_start (xlator_t *this, dict_t *dict);

int
pump_execute_start (call_frame_t *frame, xlator_t *this);

gf_boolean_t
pump_command_pause (xlator_t *this, dict_t *dict);

int
pump_execute_pause (call_frame_t *frame, xlator_t *this);

gf_boolean_t
pump_command_abort (xlator_t *this, dict_t *dict);

int
pump_execute_abort (call_frame_t *frame, xlator_t *this);

gf_boolean_t
pump_command_status (xlator_t *this, dict_t *dict);

int
pump_execute_status (call_frame_t *frame, xlator_t *this);

#endif /* __PUMP_H__ */
