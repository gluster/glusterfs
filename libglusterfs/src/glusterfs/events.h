/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EVENTS_H__
#define __EVENTS_H__

#ifdef USE_EVENTS
#include "eventtypes.h"
int
_gf_event(eventtypes_t event, const char *fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)));
#else
__attribute__((__format__(__printf__, 2, 3))) static inline int
_gf_event(eventtypes_t event, const char *fmt, ...)
{
    return 0;
}
#endif /* USE_EVENTS */

#define gf_event(event, fmt...)                                                \
    do {                                                                       \
        FMT_WARN(fmt);                                                         \
        _gf_event(event, ##fmt);                                               \
    } while (0)

#endif /* __EVENTS_H__ */
