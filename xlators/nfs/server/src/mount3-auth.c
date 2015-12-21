/*
   Copyright 2014-present Facebook. All Rights Reserved

   This file is part of GlusterFS.

   Author :
   Shreyas Siravara <shreyas.siravara@gmail.com>

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/* This file contains code for handling mount authentication.
 * The primary structure here is 'mnt3_auth_params' which contains
 * 3 important fields: 1) Pointer to a netgroups file struct, 2) Pointer to an
 * exports file struct. 3) Pointer to a mount state struct.
 *
 * - The auth parameter struct belongs to a mount state so the mount state
 *   pointer represents the mount state that this auth parameter struct belongs
 *   to.
 *
 * - Currently, the only supported mount auth parameters are an exports file
 *   and a netgroups file. The two pointers in the struct represent the files
 *   we are to authenticate against.
 *
 * - To initialize a struct, make a call to mnt3_auth_params_init () with a mnt
 *   state as a parameter.
 *
 * - To set an exports file authentication parameter, call
 *   mnt3_auth_set_exports_auth () with an exports file as a parameter.
 *
 * - Same goes for the netgroups file parameter, except use the netgroups file
 *   as the parameter.
 */

#include "mount3-auth.h"
#include "exports.h"
#include "netgroups.h"
#include "mem-pool.h"
#include "nfs-messages.h"

/**
 * mnt3_auth_params_init -- Initialize the mount3 authorization parameters
 *                          and return the allocated struct. The mount3_state
 *                          parameter is pointed to by a field in the struct.
 *
 * @ms: Mount state that is needed for auth.
 *
 * @return: success: Pointer to the allocated struct
 *          failure: NULL
 *
 * For external use.
 */
struct mnt3_auth_params *
mnt3_auth_params_init (struct mount3_state *ms)
{
        struct mnt3_auth_params *auth_params = NULL;

        auth_params = GF_MALLOC (sizeof (*auth_params),
                                 gf_nfs_mt_mnt3_auth_params);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, auth_params, out);

        auth_params->ngfile = NULL;
        auth_params->expfile = NULL;
        auth_params->ms = ms;
out:
        return auth_params;
}

/**
 * mnt3_auth_params_deinit -- Free the memory used by the struct.
 *
 * @auth_params: Pointer to the struct we want to free
 *
 * For external use.
 */
void
mnt3_auth_params_deinit (struct mnt3_auth_params *auth_params)
{
        if (!auth_params)
                goto out;

        /* Atomically set the auth params in the mount state to NULL
         * so subsequent fops will be denied while the auth params
         * are being cleaned up.
         */
        (void)__sync_lock_test_and_set (&auth_params->ms->auth_params, NULL);

        ng_file_deinit (auth_params->ngfile);
        exp_file_deinit (auth_params->expfile);
        auth_params->ms = NULL;
        GF_FREE (auth_params);
out:
        return;
}

/**
 * mnt3_set_exports_auth -- Set the exports auth file
 *
 * @auth_params : Pointer to the auth params struct
 * @filename    : File name to load from disk and parse
 *
 * @return  : success: 0
 *            failure: -1
 *
 * For external use.
 */
int
mnt3_auth_set_exports_auth (struct mnt3_auth_params *auth_params,
                            const char *filename)
{
        struct exports_file *expfile = NULL;
        struct exports_file *oldfile = NULL;
        int                  ret     = -EINVAL;

        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, auth_params, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, filename, out);

        /* Parse the exports file and set the auth parameter */
        ret = exp_file_parse (filename, &expfile, auth_params->ms);
        if (ret < 0) {
                gf_msg (GF_MNT_AUTH, GF_LOG_ERROR, 0, NFS_MSG_LOAD_PARSE_ERROR,
                        "Failed to load & parse file"
                        " %s, see logs for more information", filename);
                goto out;
        }

        /* Atomically set the file pointer */
        oldfile = __sync_lock_test_and_set (&auth_params->expfile, expfile);
        exp_file_deinit (oldfile);
        ret = 0;
out:
        return ret;
}

/**
 * mnt3_set_netgroups_auth -- Set netgroups auth file
 *
 * @auth_params : Pointer to the auth params struct.
 * @filename    : File name to load from disk and parse
 *
 * @return  : success: 0
 *            failure: -1
 *
 * For external use.
 */
int
mnt3_auth_set_netgroups_auth (struct mnt3_auth_params *auth_params,
                              const char *filename)
{
        struct netgroups_file *ngfile  = NULL;
        struct netgroups_file *oldfile = NULL;
        int                    ret     = -EINVAL;

        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, auth_params, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, filename, out);

        ngfile = ng_file_parse (filename);
        if (!ngfile) {
                gf_msg (GF_MNT_AUTH, GF_LOG_ERROR, 0, NFS_MSG_LOAD_PARSE_ERROR,
                        "Failed to load file %s, see logs for more "
                        "information", filename);
                ret = -1;
                goto out;
        }

        /* Atomically set the file pointer */
        oldfile = __sync_lock_test_and_set (&auth_params->ngfile, ngfile);
        ng_file_deinit (oldfile);
        ret = 0;
out:
        return ret;
}

/* Struct used to pass parameters to
 * _mnt3_auth_subnet_match () which
 * checks if an IP matches a subnet
 */
struct _mnt3_subnet_match_s {
        char                  *ip;   /* IP address to match */
        struct export_item   **host; /* Host structure to set */
};

/**
 * _mnt3_auth_subnet_match -- Check if an ip (specified in the parameter tmp)
 *                            is in the subnet specified by key.
 *
 * @dict: The dict to walk
 * @key : The key we are on
 * @val : The value we are on
 * @tmp : Parameter that points to the above struct
 *
 */
static int
_mnt3_auth_subnet_match (dict_t *dict, char *key, data_t *val, void *tmp)
{
        struct  _mnt3_subnet_match_s *match = NULL;

        match = (struct _mnt3_subnet_match_s *)tmp;

        if (!match)
                return 0;

        if (!match->host)
                return 0;

        if (!match->ip)
                return 0;

        /* Already found the host */
        if (*(match->host))
                return 0;

        /* Don't process anything that's not in CIDR */
        if (!strchr (key, '/'))
                return 0;

        /* Strip out leading whitespaces */
        while (*key == ' ')
                key++;

        /* If we found that the IP was in the network, set the host
         * to point to the value in the dict.
         */
        if (gf_is_ip_in_net (key, match->ip)) {
                *(match->host) = (struct export_item *)val->data;
        }
        return 0;
}

/**
 * _find_host_in_export -- Find a host in the exports file.
 *
 * Case 1: FH is non-null
 * -----------------------
 * The lookup process is two-step: The FH has a mountid which represents the
 * export that was mounted by the client. The export is defined as an entry in
 * the exports file. The FH's 'mountid' is hashed in the exports file to lookup
 * an export directory.
 *
 * Case 2: FH is null
 * -------------------
 * The lookup process is two-step: You need a directory and a hostname
 * to do the lookup. We first lookup the export directory in the file
 * and then do a lookup on the directory to find the host. If the host
 * is not found, we must finally check for subnets and then do a match.
 *
 * @file: Exports file to lookup in
 * @dir : Directory to do the lookup
 * @host: Host to lookup in the directory
 *
 * Not for external use.
 */
static struct export_item *
_mnt3_auth_check_host_in_export (const struct exports_file *file,
                                 const char *dir, const char *host,
                                 struct nfs3_fh *fh)
{
        struct export_dir           *expdir = NULL;
        struct export_item          *host_s = NULL;
        struct _mnt3_subnet_match_s  snet_match_s = {0, };

        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, file, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, host, out);

        /* If the filehandle is defined, use that to perform authentication.
         * All file operations that need authentication must follow this
         * code path.
         */
        if (fh) {
                expdir = exp_file_dir_from_uuid (file, fh->mountid);
                if (!expdir)
                        goto out;
        } else {
                /* Get the exports directory from the exports file */
                expdir = exp_file_get_dir (file, dir);
                if (!expdir)
                        goto out;
        }

        /* Extract the host from the export directory */
        host_s = exp_dir_get_host (expdir, host);
        if (!host_s)
                goto subnet_match;
        else
                goto out;

        /* If the host is not found, we need to walk through the hosts
         * in the exports directory and see if any of the "hosts" are actually
         * networks (e.g. 10.5.153.0/24). If they are we should match the
         * incoming network.
         */
subnet_match:
        if (!expdir->hosts)
                goto out;
        snet_match_s.ip = (char *)host;
        snet_match_s.host = &host_s;
        dict_foreach (expdir->hosts, _mnt3_auth_subnet_match, &snet_match_s);
out:
        return host_s;
}

/* This struct represents all the parameters necessary to search through a
 * netgroups file to find a host.
 */
struct ng_auth_search {
        const char                  *search_for; /* strings to search for */
        gf_boolean_t                 found;      /* mark true once found */
        const struct netgroups_file *file;       /* netgroups file to search */
        const char                  *expdir;
        struct export_item          *expitem;    /* pointer to the export */
        const struct exports_file   *expfile;
        gf_boolean_t                 _is_host_dict; /* searching a host dict? */
        struct netgroup_entry       *found_entry;   /* the entry we found! */
};

/**
 * __netgroup_dict_search -- Function to search the netgroups dict.
 *
 * @dict: The dict we are walking
 * @key : The key we are on
 * @val : The value associated with that key
 * @data: Additional parameters. We pass a pointer to ng_auth_search_s
 *
 * This is passed as a function pointer to dict_foreach ().
 *
 * Not for external use.
 */
static int
__netgroup_dict_search (dict_t *dict, char *key, data_t *val, void *data)
{
        struct ng_auth_search *ngsa    = NULL;
        struct netgroup_entry *ngentry = NULL;
        data_t                *hdata   = NULL;

        /* 'ngsa' is the search params */
        ngsa    = (struct ng_auth_search *)data;
        ngentry = (struct netgroup_entry *)val->data;

        if (ngsa->_is_host_dict) {
                /* If are on a host dict, we can simply hash the search key
                 * against the host dict and see if we find anything.
                 */
                hdata = dict_get (dict, (char *)ngsa->search_for);
                if (hdata) {
                        /* If it was found, log the message, mark the search
                         * params dict as found and return.
                         */
                        gf_msg_debug (GF_MNT_AUTH, errno, "key %s was hashed "
                                      "and found", key);
                        ngsa->found = _gf_true;
                        ngsa->found_entry = (struct netgroup_entry *)hdata->data;
                        goto out;
                }
        }

        /* If the key is what we are searching for, mark the item as
         * found and return.
         */
        if (strcmp (key, ngsa->search_for) == 0) {
                ngsa->found = _gf_true;
                ngsa->found_entry = ngentry;
                goto out;
        }

        /* If we have a netgroup hosts dict, then search the dict using this
         * same function.
         */
        if (ngentry->netgroup_hosts) {
                ngsa->_is_host_dict = _gf_true;
                dict_foreach (ngentry->netgroup_hosts, __netgroup_dict_search,
                                                        ngsa);
        }

        /* If that search was successful, just return */
        if (ngsa->found)
                goto out;

        /* If we have a netgroup dict, then search the dict using this same
         * function.
         */
        if (ngentry->netgroup_ngs) {
                ngsa->_is_host_dict = _gf_false;
                dict_foreach (ngentry->netgroup_ngs, __netgroup_dict_search,
                                                        ngsa);
        }
out:
        return 0;
}

/**
 * __export_dir_lookup_netgroup -- Function to search an exports directory
 *                                 for a host name.
 *
 * This function walks all the netgroups & hosts in an export directory
 * and tries to match it with the search key. This function calls the above
 * netgroup search function to search through the netgroups.
 *
 * This function is very similar to the above function, but both are necessary
 * since we are walking two different dicts. For each netgroup in _this_ dict
 * (the exports dict) we are going to find the corresponding netgroups dict
 * and walk that (nested) structure until we find the host we are looking for.
 *
 * @dict: The dict we are walking
 * @key : The key we are on
 * @val : The value associated with that key
 * @data: Additional parameters. We pass a pointer to ng_auth_search_s
 *
 * This is passed as a function pointer to dict_foreach ().
 *
 * Not for external use.
 */
static int
__export_dir_lookup_netgroup (dict_t *dict, char *key, data_t *val,
                                void *data)
{
        struct ng_auth_search *ngsa    = NULL; /* Search params */
        struct netgroups_file *nfile   = NULL; /* Netgroups file to search */
        struct netgroup_entry *ngentry = NULL; /* Entry in the netgroups file */
        struct export_dir     *tmpdir  = NULL;

        ngsa  = (struct ng_auth_search *)data;
        nfile = (struct netgroups_file *)ngsa->file;

        GF_ASSERT ((*key == '@'));

        /* We use ++key here because keys start with '@' for ngs */
        ngentry = ng_file_get_netgroup (nfile, (key + 1));
        if (!ngentry) {
                gf_msg_debug (GF_MNT_AUTH, 0, "%s not found in %s",
                              key, nfile->filename);
                goto out;
        }

        tmpdir = exp_file_get_dir (ngsa->expfile, ngsa->expdir);
        if (!tmpdir)
                goto out;

        ngsa->expitem = exp_dir_get_netgroup (tmpdir, key);
        if (!ngsa->expitem)
                goto out;

        /* Run through the host dict */
        if (ngentry->netgroup_hosts) {
                ngsa->_is_host_dict = _gf_true;
                dict_foreach (ngentry->netgroup_hosts, __netgroup_dict_search,
                              ngsa);
        }

        /* If the above search was successful, just return */
        if (ngsa->found)
                goto out;

        /* Run through the netgroups dict */
        if (ngentry->netgroup_ngs) {
                ngsa->_is_host_dict = _gf_false;
                dict_foreach (ngentry->netgroup_ngs, __netgroup_dict_search,
                              ngsa);
        }
out:
        return 0;
}

/**
 * _mnt3_auth_setup_search_param -- This function sets up an ng_auth_search
 *                                  struct with host and file as the parameters.
 *                                  Host is what we are searching for and file
 *                                  is what we are searching in.
 * @params: Search params to setup
 * @host  : The host to set
 * @nfile : The netgroups file to set
 *
 */
void _mnt3_auth_setup_search_params (struct ng_auth_search *params,
                                     const char *host, const char *dir,
                                     const struct netgroups_file *nfile,
                                     const struct exports_file *expfile)
{
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, params, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, host, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, nfile, out);

        params->search_for = host;
        params->found = _gf_false;
        params->file = nfile;
        params->_is_host_dict = _gf_false;
        params->found_entry = NULL;
        params->expitem = NULL;
        params->expfile = expfile;
        params->expdir = dir;
out:
        return;
}

/**
 * _mnt3_auth_find_host_in_netgroup -- Given a host name for an directory
 *                                     find if that hostname is in the
 *                                     directory's dict of netgroups.
 * @nfile: Netgroups file to search
 * @efile: Exports file to search
 * @dir  : The exports directory name (used to lookup in exports file)
 * @host : The host we are searching for
 *
 * Search procedure:
 *
 * - Lookup directory string against exports file structure,
 *   get an exports directory structure.
 * - Walk the export file structure's netgroup dict. This dict
 *   holds each netgroup that is authorized to mount that directory.
 * - We then have to walk the netgroup structure, which is a set of
 *   nested dicts until we find the host we are looking for.
 *
 * @return: success: Pointer to the netgroup entry found
 *          failure: NULL
 *
 * Not for external use.
 */
static struct netgroup_entry *
_mnt3_auth_check_host_in_netgroup (const struct mnt3_auth_params *auth_params,
                                   struct nfs3_fh *fh, const char *host,
                                   const char *dir, struct export_item **item)
{
        struct export_dir     *expdir      = NULL;
        struct ng_auth_search  ngsa        = {0, };
        struct netgroup_entry *found_entry = NULL;
        struct exports_file   *efile       = auth_params->expfile;
        struct netgroups_file *nfile       = auth_params->ngfile;

        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, nfile, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, efile, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, host, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, item, out);

        if (fh) {
                expdir = exp_file_dir_from_uuid (efile, fh->mountid);
                if (!expdir)
                        goto out;
        } else {
                /* Get the exports directory */
                expdir = exp_file_get_dir (efile, dir);
                if (!expdir)
                        goto out;
        }

        /* Setup search struct */
        _mnt3_auth_setup_search_params (&ngsa, host, expdir->dir_name, nfile,
                                        efile);

        /* Do the search */
        dict_foreach (expdir->netgroups, __export_dir_lookup_netgroup, &ngsa);
        found_entry = ngsa.found_entry;
        *item = ngsa.expitem;
out:
        return found_entry;
}

/**
 * check_rw_access -- Checks if the export item
 * has read-write access.
 *
 * @host_item : The export item to check
 *
 * @return -EROFS if it does not have rw access, 0 otherwise
 *
 */
int
check_rw_access (struct export_item *item)
{
        struct export_options *opts   = NULL;
        int                    ret    = -EROFS;

        if (!item)
                goto out;

        opts = item->opts;
        if (!opts)
                goto out;

        if (opts->rw)
                ret = 0;
out:
        return ret;
}

/**
 * mnt3_auth_host -- Check if a host is authorized for a directory
 *
 * @auth_params : Auth parameters to authenticate against
 * @host: Host requesting the directory
 * @dir : Directory that the host requests
 * @fh  : The filehandle passed from an fop to authenticate
 *
 * 'fh' is null on mount requests and 'dir' is null on fops
 *
 * Procedure:
 *
 * - Check if the host is in the exports directory.
 * - If not, check if the host is in the netgroups file for the
 *   netgroups authorized for the exports.
 *
 * @return: 0 if authorized
 *          -EACCES for completely unauthorized fop
 *          -EROFS  for unauthorized write operations (rm, mkdir, write)  *
 */
int
mnt3_auth_host (const struct mnt3_auth_params *auth_params, const char *host,
                struct nfs3_fh *fh, const char *dir, gf_boolean_t is_write_op,
                struct export_item **save_item)
{
        int                  auth_status_code = -EACCES;
        struct export_item  *item             = NULL;

        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, auth_params, out);
        GF_VALIDATE_OR_GOTO (GF_MNT_AUTH, host, out);

        /* Find the host in the exports file */
        item = _mnt3_auth_check_host_in_export (auth_params->expfile, dir,
                                                    host, fh);
        if (item) {
                auth_status_code = (is_write_op) ?
                                   check_rw_access (item) : 0;
                goto out;
        }

        /* Find the host in the netgroups file for the exports directory */
        if (_mnt3_auth_check_host_in_netgroup (auth_params, fh, host, dir,
                                               &item)) {
                auth_status_code = (is_write_op) ?
                                   check_rw_access (item) : 0;
                goto out;
        }

out:
        if (save_item)
                *save_item = item;

        return auth_status_code;
}
