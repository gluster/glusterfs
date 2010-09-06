/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is GF_FREE software; you can redistribute it and/or modify
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
#include "glusterd-volgen.h"
#include "glusterd-utils.h"

int
set_xlator_option (dict_t *dict, char *key,
                   char *value)
{
        int  ret      = 0;
        char *str     = NULL;

        str = GF_CALLOC (1, strlen (value) + 1,
                         gf_gld_mt_char);

        if (!str)
                return -1;

        strncpy (str, value, strlen (value));

        ret = dict_set_dynstr (dict, key, str);

        return ret;
}

static int32_t
set_default_options (dict_t *dict, char *volname)
{
        int     ret       = -1;

        ret = dict_set_str (dict, "volname",
                            volname);
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_POSIX_OPTION_ODIRECT,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_POSIX_OPTION_STATFSSIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_POSIX_OPTION_MANDATTR,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_POSIX_OPTION_SPANDEVICES,
                                 "1");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_POSIX_OPTION_BCKUNLINK,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_LOCKS_OPTION_TRACE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_LOCKS_OPTION_MAND,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_CLIENT_OPTION_TRANSTYPE,
                                 "tcp");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_CLIENT_OPTION_NODELAY,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOT_OPTION_THREADCOUNT,
                                 "16");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOT_OPTION_AUTOSCALING,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOT_OPTION_MINTHREADS,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOT_OPTION_MAXTHREADS,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_SERVER_OPTION_TRANSTYPE,
                                 "tcp");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_SERVER_OPTION_NODELAY,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_READSUBVOL,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_FAVCHILD,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_BCKSHCOUNT,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_DATASH,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_DATASHALGO,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_SHWINDOWSIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_METASH,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_ENTRYSH,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_DATACHANGELOG,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_METADATACHANGELOG,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_ENTRYCHANGELOG,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_REPLICATE_OPTION_STRICTREADDIR,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_STRIPE_OPTION_BLOCKSIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_STRIPE_OPTION_USEXATTR,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_DHT_OPTION_LOOKUPUNHASH,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_DHT_OPTION_MINFREEDISK,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_DHT_OPTION_UNHASHSTICKY,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_WB_OPTION_FLUSHBEHIND,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_WB_OPTION_CACHESIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_WB_OPTION_DISABLENBYTES,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_WB_OPTION_OSYNC,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_WB_OPTION_TRICKLINGWRITES,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_RA_OPTION_ATIME,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_RA_OPTION_PAGECOUNT,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOCACHE_OPTION_PRIORITY,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOCACHE_OPTION_TIMEOUT,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOCACHE_OPTION_CACHESIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOCACHE_OPTION_MINFILESIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_IOCACHE_OPTION_MAXFILESIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_QR_OPTION_PRIORITY,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_QR_OPTION_TIMEOUT,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_QR_OPTION_CACHESIZE,
                                 "on");
        if (ret)
                goto out;

        ret = set_xlator_option (dict, VOLGEN_QR_OPTION_MAXFILESIZE,
                                 "on");
        if (ret)
                goto out;

        ret = 0;

out:
        return ret;
}

int32_t
glusterd_default_xlator_options (glusterd_volinfo_t *volinfo)
{
        int ret = -1;

        volinfo->dict = dict_new ();
        if (!volinfo->dict) {
                ret = -1;
                goto out;
        }

        ret = set_default_options (volinfo->dict,
                                   volinfo->volname);
        if (ret) {
                goto out;
        }

        ret = 0;

out:
        return ret;

}

static int
__write_posix_xlator (FILE *file, dict_t *dict,
                      char *posix_directory)
{
         char      *volname                 = NULL;
        char       *opt_odirect             = NULL;
        char       *opt_statfssize          = NULL;
        char       *opt_mandattr            = NULL;
        char       *opt_spandevices         = NULL;
        char       *opt_bckunlink           = NULL;
        int         ret                     = -1;

        const char *posix_str = "volume %s-%s\n"
                "    type storage/posix\n"
                "    option directory %s\n"
                "#   option o-direct %s\n"
                "#   option export-statfs-size %s\n"
                "#   option mandate-attribute %s\n"
                "#   option span-devices %s\n"
                "#   option background-unlink %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_POSIX_OPTION_ODIRECT,
                            &opt_odirect);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_POSIX_OPTION_STATFSSIZE,
                            &opt_statfssize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_POSIX_OPTION_MANDATTR,
                            &opt_mandattr);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_POSIX_OPTION_SPANDEVICES,
                            &opt_spandevices);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_POSIX_OPTION_BCKUNLINK,
                            &opt_bckunlink);
        if (ret) {
                goto out;
        }

        fprintf (file, posix_str,
                 volname,
                 "posix",
                 posix_directory,
                 opt_odirect,
                 opt_statfssize,
                 opt_mandattr,
                 opt_spandevices,
                 opt_bckunlink);

        ret = 0;

out:
        return ret;
}

static int
__write_locks_xlator (FILE *file, dict_t *dict,
                      char *subvolume)
{
        char       *volname           = NULL;
        char       *opt_trace         = NULL;
        char       *opt_mand          = NULL;
        int         ret               = -1;

        const char *locks_str = "volume %s-%s\n"
                "    type features/locks\n"
                "#   option trace %s\n"
                "#   option mandatory %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_LOCKS_OPTION_TRACE,
                            &opt_trace);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_LOCKS_OPTION_MAND,
                            &opt_mand);
        if (ret) {
                goto out;
        }

        fprintf (file, locks_str,
                 volname,
                 "locks",
                 opt_trace,
                 opt_mand,
                 subvolume);

        ret = 0;

out:
        return ret;
}

static int
__write_client_xlator (FILE *file, dict_t *dict,
                       char *remote_subvol,
                       char *remote_host,
                       int count)
{
        char       *volname               = NULL;
        char       *opt_transtype         = NULL;
        char       *opt_nodelay           = NULL;
        int         ret                   = 0;


        const char *client_str = "volume %s-%s-%d\n"
                "    type protocol/client\n"
                "    option transport-type %s\n"
                "    option remote-host %s\n"
                "    option transport.socket.nodelay %s\n"
                "    option remote-subvolume %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_CLIENT_OPTION_TRANSTYPE,
                            &opt_transtype);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_CLIENT_OPTION_NODELAY,
                            &opt_nodelay);
        if (ret) {
                goto out;
        }

        fprintf (file, client_str,
                 volname,
                 "client",
                 count,
                 opt_transtype,
                 remote_host,
                 opt_nodelay,
                 remote_subvol);

        ret = 0;

out:
        return ret;
}

static int
__write_replace_brick_xlator (FILE *file, dict_t *dict)
{
        char       *volname               = NULL;
        char       *opt_transtype         = NULL;
        char       *opt_nodelay           = NULL;
        int         ret                   = 0;


        const char *client_str = "volume %s-%s\n"
                "    type protocol/client\n"
                "    option transport-type %s\n"
                "    option remote-port 34034\n"
                "    option transport.socket.nodelay %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_CLIENT_OPTION_TRANSTYPE,
                            &opt_transtype);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_CLIENT_OPTION_NODELAY,
                            &opt_nodelay);
        if (ret) {
                goto out;
        }

        fprintf (file, client_str,
                 volname,
                 "replace-brick",
                 opt_transtype,
                 opt_nodelay);

        ret = 0;

out:
        return ret;
}

static int
__write_pump_xlator (FILE *file, dict_t *dict,
                     char *subvolume)
{
        char *volname   = NULL;
        int   ret       = -1;

        const char *pump_str = "volume %s-%s\n"
                "    type cluster/pump\n"
                "    subvolumes %s %s-replace-brick\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        fprintf (file, pump_str,
                 volname, "pump",
                 subvolume,
                 volname);

        ret = 0;

out:
        return ret;
}

static int
__write_iothreads_xlator (FILE *file, dict_t *dict,
                          char *subvolume)
{
        char       *volname           = NULL;
        char       *opt_threadcount   = NULL;
        char       *opt_autoscaling   = NULL;
        char       *opt_minthreads    = NULL;
        char       *opt_maxthreads    = NULL;
        int         ret               = -1;

        const char *iot_str = "volume %s\n"
                "    type performance/io-threads\n"
                "    option thread-count %s\n"
                "#   option autoscaling %s\n"
                "#   option min-threads %s\n"
                "#   option max-threads %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "export-path", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOT_OPTION_THREADCOUNT,
                            &opt_threadcount);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOT_OPTION_AUTOSCALING,
                            &opt_autoscaling);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOT_OPTION_MINTHREADS,
                            &opt_minthreads);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOT_OPTION_MAXTHREADS,
                            &opt_maxthreads);
        if (ret) {
                goto out;
        }

        fprintf (file, iot_str,
                 volname,
                 opt_threadcount,
                 opt_autoscaling,
                 opt_minthreads,
                 opt_maxthreads,
                 subvolume);

        ret = 0;

out:
        return ret;
}

static int
__write_access_control_xlator (FILE *file, dict_t *dict,
                               char *subvolume)
{
        char  *volname       = NULL;
        int    ret           = -1;

        const char *ac_str = "volume %s-access-control\n"
                             "type features/access-control\n"
                             "subvolumes %s\n"
                             "end-volume\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }


        fprintf (file, ac_str, volname, subvolume);
        ret = 0;

out:
        return ret;
}

static int
__write_server_xlator (FILE *file, dict_t *dict,
                       char *subvolume)
{
        char  *volname       = NULL;
        char  *opt_transtype = NULL;
        char  *opt_nodelay   = NULL;
        int    ret           = -1;

        const char *server_str = "volume %s-%s\n"
                "    type protocol/server\n"
                "    option transport-type %s\n"
                "    option auth.addr.%s.allow *\n"
                "    option transport.socket.nodelay %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_SERVER_OPTION_TRANSTYPE,
                            &opt_transtype);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_SERVER_OPTION_NODELAY,
                            &opt_nodelay);
        if (ret) {
                goto out;
        }

        fprintf (file, server_str,
                 volname, "server",
                 opt_transtype,
                 subvolume,
                 opt_nodelay,
                 subvolume);

        ret = 0;

out:
        return ret;
}

static int
__write_replicate_xlator (FILE *file, dict_t *dict,
                          char *subvolume,
                          int replicate_count,
                          int subvol_count,
                          int count)
{
        char *volname               = NULL;
        char       *opt_readsubvol        = NULL;
        char       *opt_favchild          = NULL;
        char       *opt_bckshcount        = NULL;
        char       *opt_datash            = NULL;
        char       *opt_datashalgo        = NULL;
        char       *opt_shwindowsize      = NULL;
        char       *opt_metash            = NULL;
        char       *opt_entrysh           = NULL;
        char       *opt_datachangelog     = NULL;
        char       *opt_metadatachangelog = NULL;
        char       *opt_entrychangelog    = NULL;
        char       *opt_strictreaddir     = NULL;
        char        *subvol_str           = NULL;
        char        tmp[4096]             = {0,};
        int         ret                   = -1;
        int         subvolume_count       = 0;
        int         i                     = 0;
        int         len                   = 0;
        int         subvol_len            = 0;


        const char *replicate_str = "volume %s-%s-%d\n"
                "    type cluster/replicate\n"
                "#   option read-subvolume %s\n"
                "#   option favorite-child %s\n"
                "#   option background-self-heal-count %s\n"
                "#   option data-self-heal %s\n"
                "#   option data-self-heal-algorithm %s\n"
                "#   option data-self-heal-window-size %s\n"
                "#   option metadata-self-heal %s\n"
                "#   option entry-self-heal %s\n"
                "#   option data-change-log %s\n"
                "#   option metadata-change-log %s\n"
                "#   option entry-change-log %s\n"
                "#   option strict-readdir %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        subvolume_count = subvol_count;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_READSUBVOL,
                            &opt_readsubvol);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_FAVCHILD,
                            &opt_favchild);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_BCKSHCOUNT,
                            &opt_bckshcount);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_DATASH,
                            &opt_datash);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_DATASHALGO,
                            &opt_datashalgo);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_SHWINDOWSIZE,
                            &opt_shwindowsize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_METASH,
                            &opt_metash);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_ENTRYSH,
                            &opt_entrysh);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_DATACHANGELOG,
                            &opt_datachangelog);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_METADATACHANGELOG,
                            &opt_metadatachangelog);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_ENTRYCHANGELOG,
                            &opt_entrychangelog);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_REPLICATE_OPTION_STRICTREADDIR,
                            &opt_strictreaddir);
        if (ret) {
                goto out;
        }

        for (i = 0; i < replicate_count; i++) {
                snprintf (tmp, 4096, "%s-%d ", subvolume,
                          subvolume_count);
                len = strlen (tmp);
                subvol_len += len;
                subvolume_count++;
        }

        subvolume_count = subvol_count;
        subvol_len++;

        subvol_str = GF_CALLOC (1, subvol_len, gf_gld_mt_char);
        if (!subvol_str) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        for (i = 0; i < replicate_count ; i++) {
                snprintf (tmp, 4096, "%s-%d ", subvolume,
                          subvolume_count);
                strncat (subvol_str, tmp, strlen (tmp));
                subvolume_count++;
        }

        fprintf (file, replicate_str,
                 volname,
                 "replicate",
                 count,
                 opt_readsubvol,
                 opt_favchild,
                 opt_bckshcount,
                 opt_datash,
                 opt_datashalgo,
                 opt_shwindowsize,
                 opt_metash,
                 opt_entrysh,
                 opt_datachangelog,
                 opt_metadatachangelog,
                 opt_entrychangelog,
                 opt_strictreaddir,
                 subvol_str);


        ret = 0;

out:
        if (subvol_str)
                GF_FREE (subvol_str);
        return ret;
}

static int
__write_stripe_xlator (FILE *file, dict_t *dict,
                       char *subvolume,
                       int stripe_count,
                       int subvol_count,
                       int count)
{
        char *volname = NULL;
        char       *opt_blocksize    = NULL;
        char       *opt_usexattr     = NULL;
        char       *subvol_str       = NULL;
        char        tmp[4096]        = {0,};
        int         subvolume_count  = 0;
        int         ret              = -1;
        int         i                = 0;
        int         subvol_len        = 0;
        int         len              = 0;

        const char *stripe_str = "volume %s-%s-%d\n"
                "    type cluster/stripe\n"
                "#   option block-size %s\n"
                "#   option use-xattr %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        subvolume_count = subvol_count;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_STRIPE_OPTION_BLOCKSIZE,
                            &opt_blocksize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_STRIPE_OPTION_USEXATTR,
                            &opt_usexattr);
        if (ret) {
                goto out;
        }


        for (i = 0; i < stripe_count; i++) {
                snprintf (tmp, 4096, "%s-%d ", subvolume, subvolume_count);
                len = strlen (tmp);
                subvol_len += len;
                subvolume_count++;
        }

        subvolume_count = subvol_count;
        subvol_len++;

        subvol_str = GF_CALLOC (1, subvol_len, gf_gld_mt_char);
        if (!subvol_str) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        for (i = 0; i < stripe_count; i++) {
                snprintf (tmp, 4096, "%s-%d ", subvolume, subvolume_count);
                strncat (subvol_str, tmp, strlen (tmp));
                subvolume_count++;
        }

        fprintf (file, stripe_str,
                 volname,
                 "stripe",
                 count,
                 opt_blocksize,
                 opt_usexattr,
                 subvol_str);


        ret = 0;

out:
        if (subvol_str)
                GF_FREE (subvol_str);
        return ret;
        
}

static int
__write_distribute_xlator (FILE *file, dict_t *dict,
                           char *subvolume,
                           int dist_count)
{
        char       *volname          = NULL;
        char       *subvol_str       = NULL;
        char       tmp[4096]         = {0,};
        char       *opt_lookupunhash = NULL;
        char       *opt_minfreedisk  = NULL;
        char       *opt_unhashsticky = NULL;
        int        ret               = -1;
        int        i                 = 0;
        int        subvol_len        = 0;
        int        len               = 0;
        
        const char *dht_str = "volume %s-%s\n"
                "type cluster/distribute\n"
                "#   option lookup-unhashed %s\n"
                "#   option min-free-disk %s\n"
                "#   option unhashed-sticky-bit %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_DHT_OPTION_LOOKUPUNHASH,
                            &opt_lookupunhash);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_DHT_OPTION_MINFREEDISK,
                            &opt_minfreedisk);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_DHT_OPTION_UNHASHSTICKY,
                            &opt_unhashsticky);
        if (ret) {
                goto out;
        }

        for (i = 0; i < dist_count; i++) {
                snprintf (tmp, 4096, "%s-%d ", subvolume, i);
                len = strlen (tmp);
                subvol_len += len;
        }

        subvol_len++;
        subvol_str = GF_CALLOC (1, subvol_len, gf_gld_mt_char);
        if (!subvol_str) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        for (i = 0; i < dist_count ; i++) {
                snprintf (tmp, 4096, "%s-%d", subvolume, i);
                strncat (subvol_str, tmp, strlen (tmp));
        }

        fprintf (file, dht_str,
                 volname,
                 "dht",
                 opt_lookupunhash,
                 opt_minfreedisk,
                 opt_unhashsticky,
                 subvol_str);


        ret = 0;

out:
        if (subvol_str)
                GF_FREE (subvol_str);
        return ret;
}

static int
__write_wb_xlator (FILE *file, dict_t *dict,
                   char *subvolume)
{
        char        *volname              = NULL;
        char        *opt_flushbehind     = NULL;
        char        *opt_cachesize       = NULL;
        char        *opt_disablenbytes   = NULL;
        char        *opt_osync           = NULL;
        char        *opt_tricklingwrites = NULL;
        int          ret                 = -1;

        const char *dht_str = "volume %s-%s\n"
                "    type performance/write-behind\n"
                "#   option flush-behind %s\n"
                "#   option cache-size %s\n"
                "#   option disable-for-first-nbytes %s\n"
                "#   option enable-O_SYNC %s\n"
                "#   option enable-trickling-writes %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_WB_OPTION_FLUSHBEHIND,
                            &opt_flushbehind);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_WB_OPTION_CACHESIZE,
                            &opt_cachesize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_WB_OPTION_DISABLENBYTES,
                            &opt_disablenbytes);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_WB_OPTION_OSYNC,
                            &opt_osync);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_WB_OPTION_TRICKLINGWRITES,
                            &opt_tricklingwrites);
        if (ret) {
                goto out;
        }

        fprintf (file, dht_str,
                 volname,
                 "write-behind",
                 opt_flushbehind,
                 opt_cachesize,
                 opt_disablenbytes,
                 opt_osync,
                 opt_tricklingwrites,
                 subvolume);


        ret = 0;

out:
        return ret;
}

static int
__write_ra_xlator (FILE *file, dict_t *dict,
                   char *subvolume)
{
        char       *volname       = NULL;
        char       *opt_atime     = NULL;
        char       *opt_pagecount = NULL;
        int         ret           = -1;

        const char *ra_str = "volume %s-%s\n"
                "    type performance/read-ahead\n"
                "#   option force-atime-update %s\n"
                "#   option page-count %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_RA_OPTION_ATIME,
                            &opt_atime);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_RA_OPTION_PAGECOUNT,
                            &opt_pagecount);
        if (ret) {
                goto out;
        }

        fprintf (file, ra_str,
                 volname,
                 "read-ahead",
                 opt_atime,
                 opt_pagecount,
                 subvolume);


        ret = 0;

out:
        return ret;
}

static int
__write_iocache_xlator (FILE *file, dict_t *dict,
                        char *subvolume)
{
        char       *volname         = NULL;
        char       *opt_priority    = NULL;
        char       *opt_timeout     = NULL;
        char       *opt_cachesize   = NULL;
        char       *opt_minfilesize = NULL;
        char       *opt_maxfilesize = NULL;
        int         ret             = -1;

        const char *iocache_str = "volume %s-%s\n"
                "    type performance/io-cache\n"
                "#   option priority %s\n"
                "#   option cache-timeout %s\n"
                "#   option cache-size %s\n"
                "#   option min-file-size %s\n"
                "#   option max-file-size %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOCACHE_OPTION_PRIORITY,
                            &opt_priority);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOCACHE_OPTION_TIMEOUT,
                            &opt_timeout);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOCACHE_OPTION_CACHESIZE,
                            &opt_cachesize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOCACHE_OPTION_MINFILESIZE,
                            &opt_minfilesize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_IOCACHE_OPTION_MAXFILESIZE,
                            &opt_maxfilesize);
        if (ret) {
                goto out;
        }

        fprintf (file, iocache_str,
                 volname,
                 "io-cache",
                 opt_priority,
                 opt_timeout,
                 opt_cachesize,
                 opt_minfilesize,
                 opt_maxfilesize,
                 subvolume);


        ret = 0;

out:
        return ret;
}

static int
__write_qr_xlator (FILE *file, dict_t *dict,
                        char *subvolume)
{
        char       *volname         = NULL;
        char       *opt_priority    = NULL;
        char       *opt_timeout     = NULL;
        char       *opt_cachesize   = NULL;
        char       *opt_maxfilesize = NULL;
        int         ret             = -1;

        const char *qr_str = "volume %s-%s\n"
                "    type performance/quick-read\n"
                "#   option priority %s\n"
                "#   option cache-timeout %s\n"
                "#   option cache-size %s\n"
                "#   option max-file-size %s\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_QR_OPTION_PRIORITY,
                            &opt_priority);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_QR_OPTION_TIMEOUT,
                            &opt_timeout);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_QR_OPTION_CACHESIZE,
                            &opt_cachesize);
        if (ret) {
                goto out;
        }

        ret = dict_get_str (dict, VOLGEN_QR_OPTION_MAXFILESIZE,
                            &opt_maxfilesize);
        if (ret) {
                goto out;
        }

        fprintf (file, qr_str,
                 volname,
                 "quick-read",
                 opt_priority,
                 opt_timeout,
                 opt_cachesize,
                 opt_maxfilesize,
                 subvolume);

        ret = 0;

out:
        return ret;
}

static int
__write_statprefetch_xlator (FILE *file, dict_t *dict,
                             char *subvolume)
{
        char *volname = NULL;
        int   ret     = -1;

        const char *statprefetch_str = "volume %s\n"
                "    type performance/stat-prefetch\n"
                "    subvolumes %s\n"
                "end-volume\n\n";

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        fprintf (file, statprefetch_str,
                 volname,
                 subvolume);

        ret = 0;

out:
        return ret;
}

static int
generate_server_volfile (glusterd_brickinfo_t *brickinfo,
                         dict_t *dict,
                         const char *filename)
{
        FILE *file          = NULL;
        char  subvol[2048]  = {0,};
        char *volname       = NULL;
        int   ret           = -1;

        GF_ASSERT (filename);

        file = fopen (filename, "w+");
        if (!file) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not open file %s", filename);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                goto out;
        }

        /* Call functions in the same order
           as you'd call if you were manually
           writing a volfile top-down
        */

        ret = __write_posix_xlator (file, dict, brickinfo->path);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "posix");

        ret = __write_access_control_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "access-control");

        ret = __write_locks_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "locks");

        ret = __write_replace_brick_xlator (file, dict);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        ret = __write_pump_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "pump");

        ret = dict_set_str (dict, "export-path", brickinfo->path);
        if (ret) {
                goto out;
        }

        ret = __write_iothreads_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        ret = __write_server_xlator (file, dict, brickinfo->path);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        fclose (file);
        file = NULL;

out:
        return ret;
}

static int
generate_client_volfile (glusterd_volinfo_t *volinfo, char *filename)
{
        FILE    *file               = NULL;
        dict_t  *dict               = NULL;
        char    *volname            = NULL;
        glusterd_brickinfo_t *brick = NULL;
        char     subvol[2048]       = {0,};
        int32_t  replicate_count    = 0;
        int32_t  stripe_count       = 0;
        int32_t  dist_count         = 0;
        int32_t  num_bricks         = 0;
        int      subvol_count       = 0;
        int      count              = 0;
        int      i                  = 0;
        int      ret                = -1;

        GF_ASSERT (filename);

        volname = volinfo->volname;
        dict    = volinfo->dict;

        list_for_each_entry (brick, &volinfo->bricks, brick_list)
                num_bricks++;

        if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
                gf_log ("", GF_LOG_DEBUG,
                        "Volfile is distributed-replicated");
                replicate_count = volinfo->sub_count;
                dist_count = num_bricks / replicate_count;

        } else if (GF_CLUSTER_TYPE_STRIPE == volinfo->type) {
                gf_log ("", GF_LOG_DEBUG,
                        "Volfile is distributed-striped");
                stripe_count = volinfo->sub_count;
                dist_count = num_bricks / stripe_count;
        } else {
                gf_log ("", GF_LOG_DEBUG,
                        "Volfile is plain distributed");
                dist_count = num_bricks;
        }


        file = fopen (filename, "w+");
        if (!file) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not open file %s", filename);
                ret = -1;
                goto out;
        }

        /* Call functions in the same order
           as you'd call if you were manually
           writing a volfile top-down
        */

        count = 0;

        list_for_each_entry (brick, &volinfo->bricks, brick_list) {

                ret = __write_client_xlator (file, dict, brick->path,
                                             brick->hostname, count);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not write xlator");
                        goto out;
                }

                count++;
        }

        if (stripe_count && replicate_count) {
                gf_log ("", GF_LOG_DEBUG,
                        "Striped Replicate config not allowed");
                ret = -1;
                goto out;
        }

        if (replicate_count > 1) {
                subvol_count = 0;
                for (i = 0; i < dist_count; i++) {

                        VOLGEN_GENERATE_VOLNAME (subvol, volname, "client");

                        ret = __write_replicate_xlator (file, dict, subvol,
                                                        replicate_count,
                                                        subvol_count,
                                                        i);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Count not write xlator");
                                goto out;
                        }

                        subvol_count += replicate_count;

                }
        }

        if (stripe_count > 1) {
                subvol_count = 0;
                for (i = 0; i < dist_count; i++) {

                        VOLGEN_GENERATE_VOLNAME (subvol, volname, "client");

                        ret = __write_stripe_xlator (file, dict, subvol,
                                                     stripe_count,
                                                     subvol_count,
                                                     i);
                        if (ret) {
                                gf_log ("", GF_LOG_DEBUG,
                                        "Count not write xlator");
                                goto out;
                        }

                        subvol_count += stripe_count;
                }

        }

        if (dist_count > 1) {
                if (replicate_count) {
                        VOLGEN_GENERATE_VOLNAME (subvol, volname,
                                                 "replicate");
                } else if (stripe_count) {
                        VOLGEN_GENERATE_VOLNAME (subvol, volname,
                                                 "stripe");
                } else {
                        VOLGEN_GENERATE_VOLNAME (subvol, volname,
                                                 "client");
                }


                ret = __write_distribute_xlator (file,
                                                 dict,
                                                 subvol,
                                                 dist_count);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Count not write xlator");
                        goto out;
                }
        }


        if (dist_count > 1) {
                VOLGEN_GENERATE_VOLNAME (subvol, volname, "dht");
                ret = __write_wb_xlator (file, dict, subvol);
        }
        else if (replicate_count > 1) {
                VOLGEN_GENERATE_VOLNAME (subvol, volname, "replicate-0");
                ret = __write_wb_xlator (file, dict, subvol);
        }
        else if (stripe_count > 1) {
                VOLGEN_GENERATE_VOLNAME (subvol, volname, "stripe-0");
                ret = __write_wb_xlator (file, dict, subvol);
        }
        else {
                VOLGEN_GENERATE_VOLNAME (subvol, volname, "client-0");
                ret = __write_wb_xlator (file, dict, subvol);
        }
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }


        VOLGEN_GENERATE_VOLNAME (subvol, volname, "write-behind");
        ret = __write_ra_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "read-ahead");
        ret = __write_iocache_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "io-cache");
        ret = __write_qr_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        VOLGEN_GENERATE_VOLNAME (subvol, volname, "quick-read");
        ret = __write_statprefetch_xlator (file, dict, subvol);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not write xlator");
                goto out;
        }

        fclose (file);
        file = NULL;

out:
        return ret;
}

static char *
get_brick_filename (glusterd_volinfo_t *volinfo,
                    glusterd_brickinfo_t *brickinfo)
{
        char  path[PATH_MAX]   = {0,};
        char *ret              = NULL;
        char  brick[PATH_MAX]  = {0,};
        char *filename         = NULL;

        filename = GF_CALLOC (1, PATH_MAX, gf_gld_mt_char);
        if (!filename)
                goto out;

        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, brick);
        VOLGEN_GET_VOLUME_DIR (path, volinfo);

        snprintf (filename, PATH_MAX, "%s/%s.%s.%s.vol",
                  path, volinfo->volname,
                  brickinfo->hostname,
                  brick);

        ret = filename;
out:
        return ret;
}

char *
glusterd_get_nfs_filepath ()
{
        char  path[PATH_MAX] = {0,};
        char *ret            = NULL;
        char *filepath       = NULL;

        filepath = GF_CALLOC (1, PATH_MAX, gf_common_mt_char);
        if (!filepath) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate nfs file path");
                goto out;
        }

        VOLGEN_GET_NFS_DIR (path);

        snprintf (filepath, PATH_MAX, "%s/nfs-server.vol", path);

        ret = filepath;
out:
        return ret;
}


static char *
get_client_filepath (glusterd_volinfo_t *volinfo)
{
        char  path[PATH_MAX] = {0,};
        char *ret            = NULL;
        char *filename       = NULL;

        filename = GF_CALLOC (1, PATH_MAX, gf_gld_mt_char);
        if (!filename)
                goto out;

        VOLGEN_GET_VOLUME_DIR (path, volinfo);

        snprintf (filename, PATH_MAX, "%s/%s-fuse.vol",
                  path, volinfo->volname);

        ret = filename;
out:
        return ret;
}

static int
generate_brick_volfiles (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t *brickinfo = NULL;
        char                 *filename  = NULL;
        int ret = -1;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_log ("", GF_LOG_DEBUG,
                        "Found a brick - %s:%s", brickinfo->hostname,
                        brickinfo->path);

                filename = get_brick_filename (volinfo, brickinfo);
                if (!filename) {
                        gf_log ("", GF_LOG_ERROR,
                                "Out of memory");
                        ret = -1;
                        goto out;
                }

                ret = generate_server_volfile (brickinfo, volinfo->dict,
                                               filename);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not generate volfile for brick %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                if (filename)
                        GF_FREE (filename);
        }

        ret = 0;
out:
        return ret;
}

static int
glusterfsd_write_nfs_xlator (int fd, char *subvols, char *volume_ids)
{
        char    *dup_subvols = NULL;
        char    *subvols_remain = NULL;
        char    *subvol      = NULL;
        char    *str         = NULL;
        char    *free_ptr    = NULL;
        const char *nfs_str = "volume nfs-server\n"
                              "type nfs/server\n";

        if (fd <= 0)
                return -1;

        dup_subvols = gf_strdup (subvols);
        if (!dup_subvols)
                return -1;
        else
                free_ptr = dup_subvols;

        write (fd, nfs_str, strlen(nfs_str));

        subvol = strtok_r (dup_subvols, " \n", &subvols_remain);
        while (subvol) {
                str = "option rpc-auth.addr.";
                write (fd, str, strlen (str));
                write (fd, subvol, strlen (subvol));
                str = ".allow *\n";
                write (fd, str, strlen (str));
                subvol = strtok_r (NULL, " \n", &subvols_remain);
        }
        str = "option nfs.dynamic-volumes on\n";
        write (fd, str, strlen (str));

        /* Write fsids */
        write (fd, volume_ids, strlen (volume_ids));

        str = "subvolumes ";
        write (fd, str, strlen (str));
        write (fd, subvols, strlen (subvols));
        str = "\nend-volume\n";
        write (fd, str, strlen (str));
        GF_FREE (free_ptr);

        return 0;
}

int
volgen_generate_nfs_volfile (glusterd_volinfo_t *volinfo)
{
        char               *nfs_filepath             = NULL;
        char               *fuse_filepath            = NULL;
        int                 nfs_fd                   = -1;
        int                 fuse_fd                  = -1;
        int                 ret                      = -1;
        char                nfs_orig_path[PATH_MAX]  = {0,};
        char               *pad                      = NULL;
        char               *nfs_subvols              = NULL;
        char                fuse_subvols[2048]       = {0,};
        int                 subvol_len               = 0;
        char               *nfs_vol_id               = NULL;
        char                nfs_vol_id_opt[512]      = {0,};
        char                volume_id[64]            = {0,};
        int                 nfs_volid_len            = 0;
        glusterd_volinfo_t *voliter                  = NULL;
        glusterd_conf_t    *priv                     = NULL;
        xlator_t           *this                     = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        if (!volinfo) {
                gf_log ("", GF_LOG_ERROR, "Invalid Volume info");
                goto out;
        }

        nfs_filepath = glusterd_get_nfs_filepath (volinfo);
        if (!nfs_filepath)
                goto out;

        strncat (nfs_filepath, ".tmp", PATH_MAX);
        nfs_fd = open (nfs_filepath, O_WRONLY|O_TRUNC|O_CREAT, 0666);
        if (nfs_fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Could not open file: %s",
                        nfs_filepath);
                goto out;
        }


        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status != GLUSTERD_STATUS_STARTED)
                        continue;
                else {
                        subvol_len += (strlen (voliter->volname) + 1); // ' '
                        // "option nfs3.<volume>.volume-id <uuid>\n"
                        nfs_volid_len += (7 + 4 + 11 + 40 +
                                          strlen (voliter->volname));
                }
        }

        if (subvol_len == 0) {
                gf_log ("", GF_LOG_ERROR, "No volumes started");
                ret = -1;
                goto out;
        }
        subvol_len++; //null character
        nfs_subvols = GF_CALLOC (subvol_len, sizeof(*nfs_subvols),
                                 gf_common_mt_char);
        if (!nfs_subvols) {
                gf_log ("", GF_LOG_ERROR, "Memory not available");
                ret = -1;
                goto out;
        }

        nfs_vol_id = GF_CALLOC (nfs_volid_len, sizeof (char),
                                 gf_common_mt_char);
        if (!nfs_vol_id) {
                gf_log ("", GF_LOG_ERROR, "Memory not available");
                ret = -1;
                goto out;
        }

        voliter = NULL;
        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status != GLUSTERD_STATUS_STARTED)
                        continue;

                gf_log ("", GF_LOG_DEBUG,
                        "adding fuse info of - %s", voliter->volname);

                snprintf (fuse_subvols, sizeof(fuse_subvols), " %s", voliter->volname);
                fuse_filepath = get_client_filepath (voliter);
                if (!fuse_filepath) {
                        ret = -1;
                        goto out;
                }

                fuse_fd = open (fuse_filepath, O_RDONLY);
                if (fuse_fd < 0) {
                        gf_log ("", GF_LOG_ERROR, "Could not open file: %s",
                                fuse_filepath);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_file_copy (nfs_fd, fuse_fd);
                if (ret)
                        goto out;
                GF_FREE (fuse_filepath);
                fuse_filepath = NULL;
                close (fuse_fd);
                fuse_fd = -1;
                if (subvol_len > strlen (fuse_subvols)) {
                        strncat (nfs_subvols, fuse_subvols, subvol_len - 1);
                        subvol_len -= strlen (fuse_subvols);
                } else {
                        ret = -1;
                        gf_log ("", GF_LOG_ERROR, "Too many subvolumes");
                        goto out;
                }
                uuid_unparse (voliter->volume_id, volume_id);
                snprintf (nfs_vol_id_opt, 512, "option nfs3.%s.volume-id %s\n",
                          voliter->volname, volume_id);
                strcat (nfs_vol_id, nfs_vol_id_opt);
        }

        ret = glusterfsd_write_nfs_xlator (nfs_fd, nfs_subvols, nfs_vol_id);
        if (ret)
                goto out;

        strncpy (nfs_orig_path, nfs_filepath, PATH_MAX);
        pad = strrchr (nfs_orig_path, '.');
        if (!pad) {
                gf_log ("", GF_LOG_ERROR, "Failed to find the pad in nfs pat");
                ret = -1;
                goto out;
        }
        *pad = '\0';
        ret = rename (nfs_filepath, nfs_orig_path);
out:
        if (ret && nfs_filepath)
                unlink (nfs_filepath);
        if (fuse_filepath)
                GF_FREE (fuse_filepath);
        if (nfs_filepath)
                GF_FREE (nfs_filepath);
        if (nfs_subvols)
                GF_FREE (nfs_subvols);
        if (fuse_fd > 0)
                close (fuse_fd);
        if (nfs_fd > 0)
                close (nfs_fd);
        return ret;
}

static int
generate_client_volfiles (glusterd_volinfo_t *volinfo)
{
        char                 *filename    = NULL;
        int                   ret         = -1;

        filename = get_client_filepath (volinfo);
        if (!filename) {
                gf_log ("", GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        ret = generate_client_volfile (volinfo, filename);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not generate volfile for client");
                goto out;
        }

out:
        if (filename)
                GF_FREE (filename);

        return ret;
}

int
glusterd_create_volfiles (glusterd_volinfo_t *volinfo)
{
        int ret = -1;

        if(volinfo->transport_type == GF_TRANSPORT_RDMA) {
                ret = set_xlator_option (volinfo->dict, VOLGEN_CLIENT_OPTION_TRANSTYPE,
                                        "rdma");
                ret = set_xlator_option (volinfo->dict, VOLGEN_SERVER_OPTION_TRANSTYPE,
                                        "rdma");
        } else {
                ret = set_xlator_option (volinfo->dict, VOLGEN_CLIENT_OPTION_TRANSTYPE,
                                 "tcp");
                ret = set_xlator_option (volinfo->dict, VOLGEN_SERVER_OPTION_TRANSTYPE,
                                 "tcp");
        }
        ret = generate_brick_volfiles (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not generate volfiles for bricks");
                goto out;
        }

        ret = generate_client_volfiles (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "Could not generate volfile for client");
                goto out;
        }

        ret = glusterd_fetchspec_notify (THIS);

out:
        return ret;
}

int
glusterd_delete_volfile (glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo)
{
        char                    *filename  = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        filename = get_brick_filename (volinfo, brickinfo);

        if (filename)
                unlink (filename);

        if (filename)
                GF_FREE (filename);
        return 0;
}
