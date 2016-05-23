/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-common-init.c
 * This file contains common xlator loading functions, and options.
 */

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "statedump.h"

#include "rio-common.h"
#include "rio-common-helpers.h"
#include "layout.h"

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        /* TODO: Search and use a define, to distinguish server/client when
        using the string "rio" for messages */
        GF_VALIDATE_OR_GOTO ("rio", this, out);

        ret = xlator_mem_acct_init (this, gf_rio_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        RIO_MSG_VOLUME_CONF_ERROR,
                        "Memory accounting init failed");
                return ret;
        }
out:
        return ret;
}

void
rio_free_conf (struct rio_conf **conf)
{
        struct rio_conf *local_conf;

        if (!conf || !*conf)
                return;

        local_conf = *conf;

        layout_destroy (local_conf->d2cnf_dclayout);
        layout_destroy (local_conf->d2cnf_mdclayout);

        rio_destroy_volume_lists (local_conf);

        GF_FREE (*conf);
        *conf = NULL;

        return;
}

int32_t
rio_common_init (xlator_t *this)
{
        int ret;
        struct rio_conf *conf;
        xlator_t *child;
        xlator_list_t *children;

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        RIO_MSG_VOLUME_CONF_ERROR,
                        "Dangling volume, missing parent translators,"
                        " check volfile");
                goto err;
        }

        if (!this->children) {
                gf_msg (this->name, GF_LOG_ERROR, 0, RIO_MSG_VOLUME_CONF_ERROR,
                        "Missing children in volume graph, this (%s) is"
                        " not a leaf translator", this->name);
                return -1;
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_rio_mt_rio_conf);
        if (!conf)
                goto err;

        /* RIO subvolume list is separated into 2 lists, one that forms the
        meta-data cluster and the other that forms the data cluster. The list
        is a COLON separated subvolume names */

        /* TODO: It is sufficient to mention MDC-subvols, as all other subvols
        are DC-subvols, and will save space in the configuration
        (or vice-versa)*/

        /* TODO/BUG: Layout will contain meta-data against sub volumes. This
        necessitates having the same subvolume name on both client and brick
        graphs (even for the local subvolume on the brick). A way to break this
        dependency is to instead rely on subvolume ID rather than name. */

        /* TODO/BUG: A single list is not very amenable to insertion and
        deletion, at the same time querying the subvolume for its type is not
        scalable (if we need to contact each brick for its role) and does break
        the abstraction as the role is defined by this xlator. So when adding
        or removing entries from this list the order does not really matter
        hence we should not rely on names, or rather have static name for
        a subvolumes lifetime. Hence, when we add a subvolume, we will add it
        as "data-cluster" or "metadata-cluster", what holds that property is
        the subvolume getting added to one of these lists. Not finding a
        suitable alternative here at the moment */

        /* TODO/BUG: We need to know local subvolume on the brick graph, so that
        we can take appropriate action in the notify (i.e notify only on local
        subvolume being up/down etc.). Need to either fix this up right in the
        notification mechanisim, or find better alternatives */

        /* TODO/BUG: subvolume order in the graph should be local first and
        then remotes next (i.e in the brick volfiles). This is because graph up
        events fail otherwise. Possibly a bug elsewhere, but needs this
        workaround at present. */

        GF_OPTION_INIT("rio-data-subvolumes",
                       conf->d2cnf_data_subvolumes, str, err);
        GF_OPTION_INIT("rio-metadata-subvolumes",
                       conf->d2cnf_metadata_subvolumes, str, err);

        /* Process the string based volume lists */
        ret = rio_process_volume_lists (this, conf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        RIO_MSG_VOLUME_CONF_ERROR,
                        "Error processing data or metadata subvolume lists."
                        " Check volfile (Data - %s : Metadata - %s)",
                        conf->d2cnf_data_subvolumes,
                        conf->d2cnf_metadata_subvolumes);
                goto err;
        }

        /* The following attribute specifies the layout type being used. This
        is made pluggable from the initial implementation, so that different
        layouts can be used, and also when stacking RIO (if that makes sense)
        it is easier to adapt the code to different layouts, than poke around in
        every function */
        GF_OPTION_INIT ("rio-layout-type-dc", conf->d2cnf_layout_type_dc,
                        str, err);
        GF_OPTION_INIT ("rio-layout-type-mdc", conf->d2cnf_layout_type_mdc,
                        str, err);

        /* Initialize the layout */
        conf->d2cnf_dclayout = layout_init (conf->d2cnf_layout_type_dc,
                                            conf->d2cnf_dc_count,
                                            &conf->d2cnf_dc_list,
                                            this->options);
        conf->d2cnf_mdclayout = layout_init (conf->d2cnf_layout_type_mdc,
                                             conf->d2cnf_mdc_count,
                                             &conf->d2cnf_mdc_list,
                                             this->options);

        /* TODO: If we end up with a common init routine for the client and
        server, then the following code in the same file makes sense */
        GF_OPTION_INIT("rio-server-local-subvol",
                       conf->d2cnf_server_local_subvol, str, err);
        if (strncmp(RIO_SERVER_NONE_SUBVOL, conf->d2cnf_server_local_subvol,
                    strlen (RIO_SERVER_NONE_SUBVOL)) != 0) {
                /* We have a defined server subvol, assuming this is a
                rio server instance */
                for (children = this->children; children;
                     children = children->next) {
                        child = children->xlator;
                        if (strcmp (conf->d2cnf_server_local_subvol,
                                    child->name) == 0) {
                                conf->d2cnf_server_local_xlator = child;
                                break;
                        }
                }

                if (conf->d2cnf_server_local_xlator == NULL) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                RIO_MSG_VOLUME_CONF_ERROR,
                                "Missing xlator for provided server local"
                                " subvolume name %s",
                                conf->d2cnf_server_local_subvol);

                        goto err;
                }
        }

#ifdef RIO_DEBUG
        {
                struct rio_subvol *d2subvol;

                list_for_each_entry (d2subvol, &conf->d2cnf_dc_list.d2svl_node,
                                     d2svl_node) {
                        gf_msg (this->name, GF_LOG_INFO, 0, 0,
                                "DCSubvolList: %s",
                                d2subvol->d2svl_xlator->name);
                }

                list_for_each_entry (d2subvol,
                                     &conf->d2cnf_mdc_list.d2svl_node,
                                     d2svl_node) {
                        gf_msg (this->name, GF_LOG_INFO, 0, 0,
                                "MDCSubvolList: %s",
                                d2subvol->d2svl_xlator->name);
                }

                gf_msg (this->name, GF_LOG_INFO, 0, 0, "ServerSubvol: %s",
                        conf->d2cnf_server_local_subvol);
        }
#endif

        /* This is held as a pointer to the similar feature provided in DHT
        so that it can/may be leveraged in a similar manner. */
        GF_OPTION_INIT ("rio-xattr-base-name", conf->d2cnf_xattr_base_name,
                        str, err);

        this->private = conf;

        return 0;
err:
        rio_free_conf (&conf);

        return -1;
}

void
rio_common_fini (xlator_t *this)
{
        struct rio_conf *conf = NULL;

        GF_VALIDATE_OR_GOTO ("rio", this, out);

        conf = this->private;
        this->private = NULL;
        if (conf) {
                rio_free_conf (&conf);
        }
out:
        return;
}

struct volume_options options[] = {
        { .key   = {"rio-data-subvolumes"},
          .type  = GF_OPTION_TYPE_STR,
          .default_value = "",
          .description = "Specifies the list of subvolumes used as the data"
                         " cluster"
        },
        { .key   = {"rio-metadata-subvolumes"},
          .type  = GF_OPTION_TYPE_STR,
          .default_value = "",
          .description = "Specifies the list of subvolumes used as the"
                         " meta-data cluster"
        },
        { .key   = {"rio-layout-type-dc"},
          .value = {LAYOUT_INODEHASH_BUCKET},
          .type  = GF_OPTION_TYPE_STR,
          .default_value = LAYOUT_INODEHASH_BUCKET,
          .description = "Specifies the layout type for the data cluster."
        },
        { .key   = {"rio-layout-type-mdc"},
          .value = {LAYOUT_STATIC_BUCKET},
          .type  = GF_OPTION_TYPE_STR,
          .default_value = LAYOUT_STATIC_BUCKET,
          .description = "Specifies the layout type for the meta-data cluster."
        },
        { .key   = {"rio-server-local-subvol"},
          .type  = GF_OPTION_TYPE_STR,
          .default_value = RIO_SERVER_NONE_SUBVOL,
          .description = "Specifies the local subvolume for use by"
                         " RIO server"
        },
        { .key = {"rio-xattr-base-name"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "trusted.glusterfs.rio",
          .description = "Base for extended attributes used by RIO "
          "translator instance, to avoid conflicts with others above or "
          "below it."
        },
        { .key   = {NULL} },
};
