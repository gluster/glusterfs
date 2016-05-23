/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: posix2-common.c
 * This file contains common routines across ds and mds posix xlators
 */

#include "glusterfs.h"
#include "logging.h"
#include "statedump.h"
#include "syscall.h"

#include "posix2-common.h"
#include "posix2-messages.h"
#include "posix2-mem-types.h"

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("posix2", this, out);

        ret = xlator_mem_acct_init (this, gf_posix2_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        POSIX2_MSG_VOLUME_CONF_ERROR,
                        "Memory accounting init failed");
                return ret;
        }
out:
        return ret;
}

void
posix2_free_conf (struct posix2_conf **conf)
{
        if (!conf || !*conf)
                return;

        GF_FREE (*conf);
        *conf = NULL;

        return;
}

int32_t
posix2_common_init (xlator_t *this)
{
        int32_t ret = 0;
        struct posix2_conf *conf;
        struct stat buf;

        if (this->children) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        POSIX2_MSG_VOLUME_CONF_ERROR,
                        "FATAL: This xlator (%s) cannot have subvolumes",
                        this->name);
                goto err;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        POSIX2_MSG_VOLUME_CONF_ERROR,
                        "Dangling volume (%s), missing parent translators,"
                        " check volfile", this->name);
                goto err;
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_posix2_mt_posix2_conf);
        if (!conf)
                goto err;

        GF_OPTION_INIT("directory",
                       conf->p2cnf_directory, str, err);
        if (!conf->p2cnf_directory) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        POSIX2_MSG_VOLUME_CONF_ERROR,
                        "xlator %s missing configuration option \"directory\"",
                        this->name);
                goto err;
        }

        /* check if directory exists */
        ret = sys_stat (conf->p2cnf_directory, &buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        POSIX2_MSG_VOLUME_CONF_ERROR,
                        "Unable to stat option \"directory\" (%s)",
                        conf->p2cnf_directory);
                goto err;
        }

        /* check if xattr support exists on provided directory */
        ret = sys_lsetxattr (conf->p2cnf_directory,
                             "trusted.glusterfs.test", "working", 8, 0);
        if (ret != -1) {
                sys_lremovexattr (conf->p2cnf_directory,
                                  "trusted.glusterfs.test");
        } else {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        POSIX2_MSG_VOLUME_CONF_ERROR,
                        "Unable to set extended attrs on directory (%s)."
                        " %s xlator needs an underlying storage capable of "
                        "supporting extended attributes as in 'man 7 xattr'",
                        conf->p2cnf_directory, this->name);
                goto err;
        }

        /* TODO: check volume-id on root of the brick.
        Is it stashed elsewhere? */

        /* all is well, move on */
        /* TODO: umask (000); */
        this->private = conf;

        return 0;
err:
        posix2_free_conf (&conf);

        return -1;
}

void
posix2_common_fini (xlator_t *this)
{
        struct posix2_conf *conf = NULL;

        GF_VALIDATE_OR_GOTO ("posix2", this, out);

        conf = this->private;
        this->private = NULL;
        if (conf) {
                posix2_free_conf (&conf);
        }
out:
        return;
}

struct volume_options options[] = {
        { .key  = {"directory"},
          .type = GF_OPTION_TYPE_PATH },
        { .key  = {NULL} },
};
