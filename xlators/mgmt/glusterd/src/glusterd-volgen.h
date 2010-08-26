/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _GLUSTERD_VOLGEN_H_
#define _GLUSTERD_VOLGEN_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include "xlator.h"
#include "protocol-common.h"
#include "glusterd.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "cli1.h"
#include "glusterd-mem-types.h"

#define VOLGEN_GET_VOLUME_DIR(path, volinfo)                            \
        do {                                                            \
                glusterd_conf_t *priv = THIS->private;                  \
                snprintf (path, PATH_MAX, "%s/vols/%s", priv->workdir,  \
                          volinfo->volname);                            \
        } while (0);                                                    \


#define VOLGEN_GET_BRICK_DIR(path, volinfo)                             \
        do {                                                            \
                glusterd_conf_t *priv = THIS->private;                  \
                snprintf (path, PATH_MAX, "%s/%s/%s/%s", priv->workdir, \
                          GLUSTERD_VOLUME_DIR_PREFIX, volinfo->volname, \
                          GLUSTERD_BRICK_INFO_DIR);                     \
        } while (0);                                                    \


#define VOLGEN_GENERATE_VOLNAME(str, volname, subvol)             \
        do {                                                      \
                snprintf (str, 2048, "%s-%s", volname, subvol);   \
        } while (0);                                              \

#define VOLGEN_POSIX_OPTION_ODIRECT "volgen_posix_option_statfssize"
#define VOLGEN_POSIX_OPTION_STATFSSIZE "volgen_posix_option_statfssize"
#define VOLGEN_POSIX_OPTION_MANDATTR "volgen_posix_option_mandattr"
#define VOLGEN_POSIX_OPTION_SPANDEVICES "volgen_posix_option_spandevices"
#define VOLGEN_POSIX_OPTION_BCKUNLINK "volgen_posix_option_bckunlink"

#define VOLGEN_LOCKS_OPTION_TRACE "volgen_locks_option_trace"
#define VOLGEN_LOCKS_OPTION_MAND "volgen_locks_option_mand"

#define VOLGEN_CLIENT_OPTION_TRANSTYPE "volgen_client_option_transtype"
#define VOLGEN_CLIENT_OPTION_NODELAY "volgen_client_option_nodelay"

#define VOLGEN_IOT_OPTION_THREADCOUNT "volgen_iot_option_threadcount"
#define VOLGEN_IOT_OPTION_AUTOSCALING "volgen_iot_option_autoscaling"
#define VOLGEN_IOT_OPTION_MINTHREADS "volgen_iot_option_minthreads"
#define VOLGEN_IOT_OPTION_MAXTHREADS "volgen_iot_option_maxthreads"

#define VOLGEN_SERVER_OPTION_TRANSTYPE "volgen_server_option_transtype"
#define VOLGEN_SERVER_OPTION_NODELAY "volgen_server_option_nodelay"

#define VOLGEN_REPLICATE_OPTION_READSUBVOL "volgen_replicate_option_readsubvol"
#define VOLGEN_REPLICATE_OPTION_FAVCHILD "volgen_replicate_option_favchild"
#define VOLGEN_REPLICATE_OPTION_BCKSHCOUNT "volgen_replicate_option_bckshcount"
#define VOLGEN_REPLICATE_OPTION_DATASH "volgen_replicate_option_datash"
#define VOLGEN_REPLICATE_OPTION_DATASHALGO "volgen_replicate_option_datashalgo"
#define VOLGEN_REPLICATE_OPTION_SHWINDOWSIZE "volgen_replicate_option_shwindowsize"
#define VOLGEN_REPLICATE_OPTION_METASH "volgen_replicate_option_metash"
#define VOLGEN_REPLICATE_OPTION_ENTRYSH "volgen_replicate_option_entrysh"
#define VOLGEN_REPLICATE_OPTION_DATACHANGELOG "volgen_replicate_option_datachangelog"
#define VOLGEN_REPLICATE_OPTION_METADATACHANGELOG "volgen_replicate_option_metadatachangelog"
#define VOLGEN_REPLICATE_OPTION_ENTRYCHANGELOG "volgen_replicate_option_entrychangelog"
#define VOLGEN_REPLICATE_OPTION_STRICTREADDIR "volgen_replicate_option_strictreaddir"

#define VOLGEN_STRIPE_OPTION_BLOCKSIZE "volgen_stripe_option_blocksize"
#define VOLGEN_STRIPE_OPTION_USEXATTR  "volgen_stripe_option_usexattr"

#define VOLGEN_DHT_OPTION_LOOKUPUNHASH "volgen_dht_option_lookupunhash"
#define VOLGEN_DHT_OPTION_MINFREEDISK "volgen_dht_option_minfreedisk"
#define VOLGEN_DHT_OPTION_UNHASHSTICKY "volgen_dht_option_unhashsticky"

#define VOLGEN_WB_OPTION_FLUSHBEHIND "volgen_wb_option_flushbehind"
#define VOLGEN_WB_OPTION_CACHESIZE "volgen_wb_option_cachesize"
#define VOLGEN_WB_OPTION_DISABLENBYTES "volgen_wb_option_disablenbytes"
#define VOLGEN_WB_OPTION_OSYNC "volgen_wb_option_osync"
#define VOLGEN_WB_OPTION_TRICKLINGWRITES "volgen_wb_option_tricklingwrites"

#define VOLGEN_RA_OPTION_ATIME "volgen_ra_option_atime"
#define VOLGEN_RA_OPTION_PAGECOUNT "volgen_ra_option_pagecount"

#define VOLGEN_IOCACHE_OPTION_PRIORITY "volgen_iocache_option_priority"
#define VOLGEN_IOCACHE_OPTION_TIMEOUT "volgen_iocache_option_timeout"
#define VOLGEN_IOCACHE_OPTION_CACHESIZE "volgen_iocache_option_cachesize"
#define VOLGEN_IOCACHE_OPTION_MINFILESIZE "volgen_iocache_option_minfilesize"
#define VOLGEN_IOCACHE_OPTION_MAXFILESIZE "volgen_iocache_option_maxfilesize"

#define VOLGEN_QR_OPTION_PRIORITY "volgen_qr_option_priority"
#define VOLGEN_QR_OPTION_TIMEOUT "volgen_qr_option_timeout"
#define VOLGEN_QR_OPTION_CACHESIZE "volgen_qr_option_cachesize"
#define VOLGEN_QR_OPTION_MAXFILESIZE "volgen_qr_option_maxfilesize"


int
glusterd_create_volfiles (glusterd_volinfo_t *volinfo);

int32_t
glusterd_default_xlator_options (glusterd_volinfo_t *volinfo);

#endif
