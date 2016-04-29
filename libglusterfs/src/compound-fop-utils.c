/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "defaults.h"
#include "default-args.h"
#include "mem-types.h"
#include "dict.h"

compound_args_t*
compound_fop_alloc (int length, glusterfs_compound_fop_t fop, dict_t *xdata)
{
        compound_args_t *args     = NULL;

        args = GF_CALLOC (1, sizeof (args), gf_mt_compound_req_t);

        if (!args)
                return NULL;

        /* fop_enum can be used by xlators to see which fops are
         * included as part of compound fop. This will help in checking
         * for compatibility or support without going through the entire
         * fop list packed.
         */
        args->fop_enum = fop;
        args->fop_length   = length;

        args->enum_list = GF_CALLOC (length, sizeof (*args->enum_list),
                                     gf_common_mt_int);

        if (!args->enum_list)
                goto out;

        args->req_list = GF_CALLOC (length, sizeof (*args->req_list),
                                     gf_mt_default_args_t);

        if (!args->req_list)
                goto out;

        if (xdata) {
                args->xdata = dict_copy_with_ref (xdata, args->xdata);
                if (!args->xdata)
                        goto out;
        }

        return args;
out:
        if (args->xdata)
                dict_unref (args->xdata);

        if (args->req_list)
                GF_FREE (args->req_list);

        if (args->enum_list)
                GF_FREE (args->enum_list);

        if (args)
                GF_FREE (args);

        return NULL;
}

#define COMPOUND_PACK_ARGS(fop, fop_enum, args, counter, params ...) do {    \
        args->enum_list[counter] = fop_enum;                                 \
        args_##fop##_store (&args->req_list[counter], params);               \
} while (0)
