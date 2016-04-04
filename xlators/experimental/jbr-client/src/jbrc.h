/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _JBRC_H_
#define _JBRC_H_

typedef struct {
        xlator_t        *active;
        uint8_t          up_children;
        uint8_t          n_children;
        uint32_t         kid_state;
} jbrc_private_t;

typedef struct {
        call_stub_t     *stub;
        xlator_t        *curr_xl;
        uint16_t        scars;
} jbrc_local_t;

#endif /* _JBRC_H_ */
