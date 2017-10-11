/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __MONITORING_H__
#define __MONITORING_H__

#include "glusterfs.h"

#define GLUSTER_METRICS_DIR "/var/run/gluster/metrics"

char *
gf_monitor_metrics (glusterfs_ctx_t *ctx);

#endif /* __MONITORING_H__ */
