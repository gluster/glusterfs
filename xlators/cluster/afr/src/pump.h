/*
   Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#define PUMP_PID 696969
#define PUMP_LK_OWNER 696969

#define IS_ROOT_PATH(path) (!strcmp (path, "/"))
#define IS_ENTRY_CWD(entry) (!strcmp (entry, "."))
#define IS_ENTRY_PARENT(entry) (!strcmp (entry, ".."))

#define PUMP_CMD_START  "trusted.glusterfs.pump.start"
#define PUMP_CMD_ABORT  "trusted.glusterfs.pump.abort"
#define PUMP_CMD_PAUSE  "trusted.glusterfs.pump.pause"
#define PUMP_CMD_STATUS "trusted.glusterfs.pump.status"

#define PUMP_SOURCE_COMPLETE "trusted.glusterfs.pump-source-complete"
#define PUMP_SINK_COMPLETE "trusted.glusterfs.pump-sink-complete"

#define PUMP_PATH "trusted.glusterfs.pump-path"

#define PUMP_SOURCE_CHILD(xl) (xl->children->xlator)
#define PUMP_SINK_CHILD(xl) (xl->children->next->xlator)

typedef enum {
        PUMP_STATE_RUNNING,
        PUMP_STATE_RESUME,
        PUMP_STATE_PAUSE,
        PUMP_STATE_ABORT,
} pump_state_t;

typedef struct _pump_private {
	struct syncenv *env;
        const char *resume_path;
        gf_lock_t resume_path_lock;
        gf_lock_t pump_state_lock;
        pump_state_t pump_state;
        long source_blocks;
        long sink_blocks;
        char current_file[PATH_MAX];
        uint64_t number_files_pumped;
        gf_boolean_t pump_finished;
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

gf_boolean_t
is_pump_loaded (xlator_t *this);

#endif /* __PUMP_H__ */
