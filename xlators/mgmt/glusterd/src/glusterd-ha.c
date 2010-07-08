/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>


#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "protocol-common.h"
//#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-ha.h"
#include "gd-xdr.h"
#include "cli-xdr.h"
#include "rpc-clnt.h"

#include <sys/resource.h>
#include <inttypes.h>

int32_t
glusterd_ha_create_volume (glusterd_volinfo_t *volinfo)
{
        char    pathname[PATH_MAX] = {0,};
        int32_t ret = -1;
        char    filepath[PATH_MAX] = {0,};
        char    buf[4096] = {0,};
        int     fd = -1;

        GF_ASSERT (volinfo);
        snprintf (pathname, 1024, "%s/vols/%s", GLUSTERD_DEFAULT_WORKDIR, 
                  volinfo->volname);

        ret = mkdir (pathname, 0x777);

        if (-1 == ret) {
                gf_log ("", GF_LOG_ERROR, "mkdir() failed on path %s,"
                        "errno: %d", pathname, errno);
                goto out;
        }

        snprintf (filepath, 1024, "%s/info", pathname);

        fd = open (filepath, O_RDWR | O_CREAT | O_APPEND, 0644); 

        if (-1 == fd) {
                gf_log ("", GF_LOG_ERROR, "open() failed on path %s,"
                        "errno: %d", filepath, errno);
                ret = -1;
                goto out;
        }

        snprintf (buf, 4096, "type=%d\n", volinfo->type);
        ret = write (fd, buf, strlen (buf));
        snprintf (buf, 4096, "count=%d\n", volinfo->brick_count);
        ret = write (fd, buf, strlen (buf));
        close (fd);

        ret = 0;

out:
        if (ret) {
                glusterd_ha_delete_volume (volinfo);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

        
int32_t
glusterd_ha_delete_volume (glusterd_volinfo_t *volinfo)
{
        char    pathname[PATH_MAX] = {0,};
        int32_t ret = -1;
        char    filepath[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);
        snprintf (pathname, 1024, "%s/vols/%s", GLUSTERD_DEFAULT_WORKDIR, 
                  volinfo->volname);

        snprintf (filepath, 1024, "%s/info", pathname);
        ret = unlink (filepath);

        if (-1 == ret) {
                gf_log ("", GF_LOG_ERROR, "unlink() failed on path %s,"
                        "errno: %d", filepath, errno);
                goto out;
        }

        ret = rmdir (pathname);

        if (-1 == ret) {
                gf_log ("", GF_LOG_ERROR, "rmdir() failed on path %s,"
                        "errno: %d", pathname, errno);
                goto out;
        }

out:

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


