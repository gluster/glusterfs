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

#include "glusterd1.h"
#include "cli1.h"
#include "rpc-clnt.h"

#include <sys/resource.h>
#include <inttypes.h>
#include <dirent.h>

int32_t
glusterd_ha_create_volume (glusterd_volinfo_t *volinfo)
{
        char                    pathname[PATH_MAX] = {0,};
        int32_t                 ret = -1;
        char                    filepath[PATH_MAX] = {0,};
        char                    buf[4096] = {0,};
        int                     fd = -1;
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volinfo);
        priv = THIS->private;

        GF_ASSERT (priv);

        snprintf (pathname, 1024, "%s/vols/%s", priv->workdir,
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
        glusterd_conf_t *priv = NULL;
        DIR     *dir = NULL;
        struct dirent *entry = NULL;
        char path[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);
        priv = THIS->private;

        GF_ASSERT (priv);
        snprintf (pathname, 1024, "%s/vols/%s", priv->workdir,
                  volinfo->volname);

        dir = opendir (pathname);
        if (!dir)
                goto out;

        entry = readdir (dir);
        while (entry != NULL) {
                if (!strcmp (entry->d_name, ".") ||
                    !strcmp (entry->d_name, "..")) {
                        entry = readdir (dir);
                        continue;
                }
                snprintf (path, PATH_MAX, "%s/%s", pathname, entry->d_name);
                if (DT_DIR  == entry->d_type)
                        ret = rmdir (path);
                else
                        ret = unlink (path);

                gf_log ("", GF_LOG_NORMAL, "%s %s",
                                ret?"Failed to remove":"Removed",
                                entry->d_name);
                if (ret)
                        gf_log ("", GF_LOG_NORMAL, "errno:%d", errno);
                entry = readdir (dir);
                memset (path, 0, sizeof(path));
        }

        ret = closedir (dir);
        if (ret) {
                gf_log ("", GF_LOG_NORMAL, "Failed to close dir, errno:%d",
                        errno);
        }

        ret = rmdir (pathname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Failed to rmdir: %s, errno: %d",
                        pathname, errno);
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


