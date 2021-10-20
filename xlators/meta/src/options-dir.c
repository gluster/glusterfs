/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>

#include "meta-mem-types.h"
#include "meta.h"
#include "meta-hooks.h"

/*
 * dir_fill for generic variant - see option-file.c
 * for details
 */
static int
dict_key_add(dict_t *dict, char *key, data_t *value, void *data)
{
    struct meta_dirent **direntp = data;

    (*direntp)->name = gf_strdup(key);
    (*direntp)->type = IA_IFREG;
    (*direntp)->hook = meta_option_file_hook;

    (*direntp)++;
    return 0;
}

static int
options_dir_fill(xlator_t *this, inode_t *inode, struct meta_dirent **dp)
{
    struct meta_dirent *dirent = NULL;
    struct meta_dirent *direntp = NULL;
    xlator_t *xl = NULL;

    xl = meta_ctx_get(inode, this);

    dirent = GF_CALLOC(sizeof(*dirent), xl->options->count,
                       gf_meta_mt_dirents_t);
    if (!dirent)
        return -1;

    direntp = dirent;

    dict_foreach(xl->options, dict_key_add, &direntp);

    *dp = dirent;

    return xl->options->count;
}

/*
 * dir_fill for adjustable variant - see option-file.c
 * for details
 */
static int
adjustable_options_dir_fill(xlator_t *this, inode_t *inode,
                            struct meta_dirent **dp)
{
    int count = 0;
    int i = 0;
    volume_opt_list_t *vol_opt_list = NULL;
    volume_option_t *vol_opt = NULL;
    struct meta_dirent *dirent = NULL;
    struct meta_dirent *direntp = NULL;
    xlator_t *xl = NULL;

    xl = meta_ctx_get(inode, this);

    list_for_each_entry(vol_opt_list, &xl->volume_options, list)
    {
        vol_opt = vol_opt_list->given_opt;
        while (vol_opt->key[0]) {
            if (vol_opt->fetch) {
                for (i = 0; i < ZR_VOLUME_MAX_NUM_KEY; i++) {
                    if (vol_opt->key[i])
                        count++;
                    else
                        break;
                }
            }
            vol_opt++;
        }
    }
    dirent = GF_CALLOC(sizeof(*dirent), count, gf_meta_mt_dirents_t);
    if (!dirent)
        return -1;

    direntp = dirent;

    list_for_each_entry(vol_opt_list, &xl->volume_options, list)
    {
        vol_opt = vol_opt_list->given_opt;
        while (vol_opt->key[0]) {
            if (vol_opt->fetch) {
                for (i = 0; i < ZR_VOLUME_MAX_NUM_KEY; i++) {
                    if (vol_opt->key[i]) {
                        direntp->name = gf_strdup(vol_opt->key[i]);
                        if (!direntp->name)
                            /*
                             * There is no way to fail the served fop from here,
                             * we can only omit entries we can't setup properly.
                             * (If we wanted to be strict, we could return with
                             * -1 / *dp = NULL, but that still would not imply
                             * a failure, just we wouldn't generate any entry;
                             * OTOH, we'd have to take the trouble of freeing
                             * allocations up to this point. It's less pain
                             * to just return early.)
                             */
                            goto out;
                        if (i == 0) {
                            direntp->type = IA_IFREG;
                            direntp->hook = meta_adjustable_option_file_hook;
                        } else {
                            direntp->type = IA_IFLNK;
                            direntp->hook = meta_adjustable_option_link_hook;
                        }
                        direntp++;
                    } else
                        break;
                }
            }
            vol_opt++;
        }
    }

out:
    *dp = dirent;

    return direntp - dirent;
}

static struct meta_ops options_dir_ops = {.dir_fill = options_dir_fill};
static struct meta_ops adjustable_options_dir_ops = {
    .dir_fill = adjustable_options_dir_fill};

int
meta_options_dir_hook(call_frame_t *frame, xlator_t *this, loc_t *loc,
                      dict_t *xdata)
{
    meta_ctx_set(loc->inode, this, meta_ctx_get(loc->parent, this));

    meta_ops_set(loc->inode, this, &options_dir_ops);

    return 0;
}

int
meta_adjustable_options_dir_hook(call_frame_t *frame, xlator_t *this,
                                 loc_t *loc, dict_t *xdata)
{
    meta_ctx_set(loc->inode, this, meta_ctx_get(loc->parent, this));

    meta_ops_set(loc->inode, this, &adjustable_options_dir_ops);

    return 0;
}
