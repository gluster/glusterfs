/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#define DEFAULT_VOLUME_SPECFILE               CONFDIR "/specfile.vol"
#define DEFAULT_LOG_FILE_DIRECTORY            DATADIR "/log/glusterfs"
#define DEFAULT_LOG_LEVEL                     GF_LOG_WARNING
#define	DEFAULT_SPECFILE_SERVER_PORT          6996
#define DEFAULT_SPECFILE_SERVER_TRANSPORT     "socket"
#define DEFAULT_FUSE_DIRECTORY_ENTRY_TIMEOUT       1
#define DEFAULT_FUSE_ATTRIBUTE_TIMEOUT             1

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

#define TRANSLATOR_TYPE_MOUNT_FUSE_STRING                          "mount/fuse"
#define TRANSLATOR_TYPE_MOUNT_FUSE_OPTION_MOUNT_POINT_STRING       "mount-point"
#define TRANSLATOR_TYPE_MOUNT_FUSE_OPTION_ATTR_TIMEOUT_STRING      "attr-timeout"
#define TRANSLATOR_TYPE_MOUNT_FUSE_OPTION_ENTRY_TIMEOUT_STRING     "entry-timeout"
#define TRANSLATOR_TYPE_MOUNT_FUSE_OPTION_DIRECT_IO_MODE_STRING    "direct-io-mode"


#define SERVER_TRANSLATOR_TYPE_STRING    "protocol/server"
#define CLIENT_TRANSLATOR_TYPE_STRING    TRANSLATOR_TYPE_MOUNT_FUSE_STRING
#define CLIENT_TRANSLATOR_TYPE_MOUNT_POINT_STRING    TRANSLATOR_TYPE_MOUNT_FUSE_OPTION_MOUNT_POINT_STRING

enum argp_option_keys {
	ARGP_SPECFILE_SERVER_KEY = 's', 
	ARGP_VOLUME_SPECFILE_KEY = 'f', 
	ARGP_LOG_LEVEL_KEY = 'L', 
	ARGP_LOG_FILE_KEY = 'l', 
	ARGP_SPECFILE_SERVER_PORT_KEY = 131, 
	ARGP_SPECFILE_SERVER_TRANSPORT_KEY = 132, 
	ARGP_PID_FILE_KEY = 'p',
	ARGP_NO_DAEMON_KEY = 'N', 
	ARGP_RUN_ID_KEY = 'r', 
	ARGP_DEBUG_KEY = 133, 
	ARGP_DISABLE_DIRECT_IO_MODE_KEY = 134, 
	ARGP_DIRECTORY_ENTRY_TIMEOUT_KEY = 135, 
	ARGP_ATTRIBUTE_TIMEOUT_KEY = 136, 
	ARGP_VOLUME_NAME_KEY = 137,
	ARGP_XLATOR_OPTION_KEY = 138,
#ifdef GF_DARWIN_HOST_OS
	ARGP_NON_LOCAL_KEY = 139,
	ARGP_ICON_NAME_KEY = 140,
#endif /* DARWIN */
	ARGP_FUSE_NODEV_KEY = 141,
	ARGP_FUSE_NOSUID_KEY = 142,
	ARGP_SPECFILE_SERVER_GETSPEC_KEY = 143, 
};

#endif /* __GLUSTERFSD_H__ */
