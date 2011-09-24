/*
   Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
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


#ifndef __MDC_MEM_TYPES_H__
#define __MDC_MEM_TYPES_H__

#include "mem-types.h"

enum gf_mdc_mem_types_ {
        gf_mdc_mt_mdc_local_t   = gf_common_mt_end + 1,
	gf_mdc_mt_md_cache_t,
	gf_mdc_mt_mdc_conf_t,
        gf_mdc_mt_end
};
#endif

