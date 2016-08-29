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

void
compound_args_cleanup (compound_args_t *args)
{
        int i;

        if (!args)
                return;

        if (args->xdata)
                dict_unref (args->xdata);

        if (args->req_list) {
                for (i = 0; i < args->fop_length; i++) {
                        args_wipe (&args->req_list[i]);
                }
        }

        GF_FREE (args->enum_list);
        GF_FREE (args->req_list);
        GF_FREE (args);
}

void
compound_args_cbk_cleanup (compound_args_cbk_t *args_cbk)
{
        int i;

        if (!args_cbk)
                return;

        if (args_cbk->xdata)
                dict_unref (args_cbk->xdata);

        if (args_cbk->rsp_list) {
                for (i = 0; i < args_cbk->fop_length; i++) {
                        args_cbk_wipe (&args_cbk->rsp_list[i]);
                }
        }

        GF_FREE (args_cbk->rsp_list);
        GF_FREE (args_cbk->enum_list);
        GF_FREE (args_cbk);
}

compound_args_cbk_t*
compound_args_cbk_alloc (int length, dict_t *xdata)
{
        int                 i             = 0;
        compound_args_cbk_t *args_cbk     = NULL;

        args_cbk = GF_CALLOC (1, sizeof (*args_cbk), gf_mt_compound_rsp_t);
        if (!args_cbk)
                return NULL;

        args_cbk->fop_length = length;

        args_cbk->rsp_list = GF_CALLOC (length, sizeof (*args_cbk->rsp_list),
                                        gf_mt_default_args_cbk_t);
        if (!args_cbk->rsp_list)
                goto out;

        for (i = 0; i < length; i++) {
                args_cbk_init (&args_cbk->rsp_list[i]);
        }

        args_cbk->enum_list = GF_CALLOC (length, sizeof (*args_cbk->enum_list),
                                        gf_common_mt_int);
        if (!args_cbk->enum_list)
                goto out;

        if (xdata) {
                args_cbk->xdata = dict_copy_with_ref (xdata, NULL);
                if (!args_cbk->xdata)
                        goto out;
        }

        return args_cbk;
out:
        compound_args_cbk_cleanup (args_cbk);
        return NULL;
}

compound_args_t*
compound_fop_alloc (int length, glusterfs_compound_fop_t fop, dict_t *xdata)
{
        compound_args_t *args     = NULL;

        args = GF_CALLOC (1, sizeof (*args), gf_mt_compound_req_t);

        if (!args)
                return NULL;

        /* fop_enum can be used by xlators to see which fops are
         * included as part of compound fop. This will help in checking
         * for compatibility or support without going through the entire
         * fop list packed.
         */
        args->fop_enum = fop;
        args->fop_length = length;

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
        compound_args_cleanup (args);
        return NULL;
}
