/*
   Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __GLUSTERFSD_H__
#define __GLUSTERFSD_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define DEFAULT_CLIENT_VOLUME_FILE            CONFDIR "/glusterfs.vol"
#define DEFAULT_SERVER_VOLUME_FILE            CONFDIR "/glusterfsd.vol"
#define DEFAULT_LOG_FILE_DIRECTORY            DATADIR "/log/glusterfs"
#define DEFAULT_LOG_LEVEL                     GF_LOG_NORMAL

#define DEFAULT_EVENT_POOL_SIZE            16384

#define ARGP_LOG_LEVEL_NONE_OPTION        "NONE"
#define ARGP_LOG_LEVEL_TRACE_OPTION       "TRACE"
#define ARGP_LOG_LEVEL_CRITICAL_OPTION    "CRITICAL"
#define ARGP_LOG_LEVEL_ERROR_OPTION       "ERROR"
#define ARGP_LOG_LEVEL_WARNING_OPTION     "WARNING"
#define ARGP_LOG_LEVEL_NORMAL_OPTION      "NORMAL"
#define ARGP_LOG_LEVEL_DEBUG_OPTION       "DEBUG"

#define ENABLE_NO_DAEMON_MODE     1
#define ENABLE_DEBUG_MODE         1

#define ZR_XLATOR_FUSE          "mount/fuse"
#define ZR_MOUNTPOINT_OPT       "mountpoint"
#define ZR_ATTR_TIMEOUT_OPT     "attribute-timeout"
#define ZR_ENTRY_TIMEOUT_OPT    "entry-timeout"
#define ZR_DIRECT_IO_OPT        "direct-io-mode"
#define ZR_STRICT_VOLFILE_CHECK "strict-volfile-check"

enum argp_option_keys {
	ARGP_VOLFILE_SERVER_KEY = 's', 
	ARGP_VOLUME_FILE_KEY = 'f', 
	ARGP_LOG_LEVEL_KEY = 'L', 
	ARGP_LOG_FILE_KEY = 'l', 
	ARGP_VOLFILE_SERVER_PORT_KEY = 131, 
	ARGP_VOLFILE_SERVER_TRANSPORT_KEY = 132, 
	ARGP_PID_FILE_KEY = 'p',
	ARGP_NO_DAEMON_KEY = 'N', 
	ARGP_RUN_ID_KEY = 'r', 
	ARGP_DEBUG_KEY = 133, 
	ARGP_DISABLE_DIRECT_IO_MODE_KEY = 134, 
	ARGP_ENTRY_TIMEOUT_KEY = 135, 
	ARGP_ATTRIBUTE_TIMEOUT_KEY = 136, 
	ARGP_VOLUME_NAME_KEY = 137,
	ARGP_XLATOR_OPTION_KEY = 138,
#ifdef GF_DARWIN_HOST_OS
	ARGP_NON_LOCAL_KEY = 139,
#endif /* DARWIN */
	ARGP_VOLFILE_ID_KEY = 143, 
        ARGP_VOLFILE_CHECK_KEY = 144,
        ARGP_VOLFILE_MAX_FETCH_ATTEMPTS = 145,
        ARGP_LOG_SERVER_KEY = 146,
        ARGP_LOG_SERVER_PORT_KEY = 147,
};

/* Moved here from fetch-spec.h */
FILE *fetch_spec (glusterfs_ctx_t *ctx);


#endif /* __GLUSTERFSD_H__ */
