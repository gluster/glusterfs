/*
   Copyright 2014-present Facebook. All Rights Reserved

   This file is part of GlusterFS.

   Author :
   Shreyas Siravara <shreyas.siravara@gmail.com>

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2),in all
   cases as published by the Free Software Foundation.
*/

#include "netgroups.h"
#include "parse-utils.h"
#include "nfs-messages.h"

static void _nge_print (const struct netgroup_entry *nge);
static void _netgroup_entry_deinit (struct netgroup_entry *ptr);
static void _netgroup_host_deinit (struct netgroup_host *host);

static dict_t *__deleted_entries;
static struct parser *ng_file_parser;
static struct parser *ng_host_parser;

/**
 * _ng_init_parser -- Initialize the parsers used in this file
 *
 * @return: success: 0 (on success the parsers are initialized)
 *          failure: -1
 */
static int
_ng_init_parsers ()
{
        int ret = -1;

        /* Initialize the parsers. The only reason this should
         * ever fail is because of 1) memory allocation errors
         * 2) the regex in netgroups.h has been changed and no
         * longer compiles.
         */
        ng_file_parser = parser_init (NG_FILE_PARSE_REGEX);
        if (!ng_file_parser)
                goto out;

        ng_host_parser = parser_init (NG_HOST_PARSE_REGEX);
        if (!ng_host_parser)
                goto out;

        ret = 0;
out:
        return ret;
}

/**
 * _ng_deinit_parsers - Free the parsers used in this file
 */
static void
_ng_deinit_parsers ()
{
        parser_deinit (ng_file_parser);
        parser_deinit (ng_host_parser);
}

/**
 * _netgroups_file_init - allocate a netgroup file struct
 * @return: success: Pointer to an allocated netgroup file struct
 *          failure: NULL
 *
 * Not for external use.
 */
static struct netgroups_file *
_netgroups_file_init ()
{
        struct netgroups_file *file  = GF_MALLOC (sizeof (*file),
                                                  gf_common_mt_nfs_netgroups);

        if (!file)
                goto out;

        file->filename     = NULL;
        file->ng_file_dict = NULL;
out:
        return file;
}

/**
 * __ngf_free_walk - walk the netgroup file dict and free each element
 *
 * This is passed as a function pointer to dict_foreach ()
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__ngf_free_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        struct netgroup_entry        *nge = NULL;

        if (val) {
                nge = (struct netgroup_entry *)val->data;
                _netgroup_entry_deinit (nge);
                val->data = NULL;
                dict_del (dict, key); /* Remove the key from this dict */
        }
        return 0;
}

/**
 * __deleted_entries_free_walk - free the strings in the temporary dict
 *
 * This is passed as a function pointer to dict_foreach ()
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__deleted_entries_free_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        dict_del (dict, key);
        return 0;
}

/**
 * ng_file_deinit - Free the netgroup file struct and any memory
 * that is allocated for its members.
 *
 * @ngfile : Pointer to the netgroup file structure that needs to be freed
 * @return : Nothing
 *
 * External facing function.
 *
 * Should be called by the caller of ng_file_parse () in order to free
 * the memory allocated when parsing the file.
 */
void
ng_file_deinit (struct netgroups_file *ngfile)
{
        if (!ngfile) {
                return;
        }

        __deleted_entries = dict_new ();
        GF_VALIDATE_OR_GOTO (GF_NG, __deleted_entries, out);

        GF_FREE (ngfile->filename);
        dict_foreach (ngfile->ng_file_dict, __ngf_free_walk, NULL);
        dict_unref (ngfile->ng_file_dict);
        GF_FREE (ngfile);

        /* Clean up temporary dict we used to store "freed" names */
        dict_foreach (__deleted_entries, __deleted_entries_free_walk, NULL);
        dict_unref (__deleted_entries);
        __deleted_entries = NULL;
out:
        return;
}

/**
 * _netgroup_entry_init - Initializes a netgroup entry struct.
 * A netgroup entry struct represents a single line in a netgroups file.
 *
 * @return : success: Pointer to a netgroup entry struct
 *         : failure: NULL
 *
 * Not for external use.
 */
static struct netgroup_entry *
_netgroup_entry_init ()
{
        struct netgroup_entry *entry = GF_CALLOC (1, sizeof (*entry),
                                                  gf_common_mt_nfs_netgroups);
        return entry;
}

/**
 * __ngh_free_walk - walk the netgroup host dict and free the host
 * structure associated with the key.
 *
 * This is passed as a function pointer to dict_foreach ()
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__ngh_free_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        struct netgroup_host *ngh = NULL;

        if (val) {
                ngh = (struct netgroup_host *)val->data;
                _netgroup_host_deinit (ngh);
                val->data = NULL;
                dict_del (dict, key);
        }
        return 0;
}

/**
 * __nge_free_walk - walk the netgroup entry dict and free the netgroup entry
 * structure associated with the key.
 *
 * This is passed as a function pointer to dict_foreach ()
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__nge_free_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        struct netgroup_entry *nge = NULL;

        GF_VALIDATE_OR_GOTO (GF_NG, dict, out);

        if (val) {
                nge = (struct netgroup_entry *)val->data;
                if (!dict_get (__deleted_entries, key)) {
                        _netgroup_entry_deinit (nge);
                        val->data = NULL;
                }
                dict_del (dict, key);
        }

out:
        return 0;
}

/**
 * _netgroup_entry_deinit - Free memory pointed to by the parameter
 *                          and any memory allocated for members
 *                          in the struct. This function walks the
 *                          netgroups and hosts dicts if they
 *                          are allocated and frees them.
 *
 * @ngentry: Pointer to a netgroup entry struct that needs to be freed
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static void
_netgroup_entry_deinit (struct netgroup_entry *ngentry)
{
        dict_t  *ng_dict   = NULL;
        dict_t  *host_dict = NULL;
        char    *name      = NULL;
        data_t  *dint      = NULL;

        if (!ngentry)
                return;

        ng_dict = ngentry->netgroup_ngs;
        host_dict = ngentry->netgroup_hosts;

        if (ng_dict) {
                /* Free the dict of netgroup entries */
                dict_foreach (ng_dict, __nge_free_walk, NULL);
                dict_unref (ng_dict);
                ngentry->netgroup_ngs = NULL;
        }

        if (host_dict) {
                /* Free the dict of host entries */
                dict_foreach (host_dict, __ngh_free_walk, NULL);
                dict_unref (host_dict);
                ngentry->netgroup_hosts = NULL;
        }

        if (ngentry->netgroup_name) {
                /* Keep track of the netgroup names we've deallocated
                 * We need to do this because of the nature of this data
                 * structure. This data structure may hold multiple
                 * pointers to an already freed object, but these are
                 * uniquely identifiable by the name. We keep track
                 * of these names so when we encounter a key who has
                 * an association to an already freed object, we don't
                 * free it twice.
                 */
                name = strdupa (ngentry->netgroup_name);

                dint = int_to_data (1);
                dict_set (__deleted_entries, name, dint);

                GF_FREE (ngentry->netgroup_name);
                ngentry->netgroup_name = NULL;
        }

        GF_FREE (ngentry);
}

/**
 * _netgroup_host_init - Initializes a netgroup host structure.
 * A netgroup host struct represents an item in a line of a netgroups file that
 * looks like this : (hostname,user,domain)
 *
 * @return : success: Pointer to a netgroup host struct
 *         : failure: NULL
 *
 * Not for external use.
 */
static struct netgroup_host *
_netgroup_host_init ()
{
        struct netgroup_host *host = GF_CALLOC (1, sizeof (*host),
                                                gf_common_mt_nfs_netgroups);
        return host;
}

/**
 * _netgroup_host_deinit - Free memory pointed to by the parameter
 * and any memory allocated for members in the struct.
 *
 * @nghost : Pointer to a netgroup host struct that needs to be freed
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static void
_netgroup_host_deinit (struct netgroup_host *host)
{
        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_NG, host, err);

        GF_FREE (host->hostname);
        host->hostname = NULL;

        GF_FREE (host->user);
        host->user = NULL;

        GF_FREE (host->domain);
        host->domain = NULL;

        GF_FREE (host);
err:
        return;
}

/**
 * _nge_dict_get - Lookup a netgroup entry from the dict based
 *                 on the netgroup name.
 *
 * @dict   : The dict we are looking up from. This function makes the
 *           assumption that the type of underlying data in the dict is of type
 *           struct netgroup_entry. The behavior is not defined otherwise.
 *
 * @ngname : Key used to lookup in the dict.
 *
 * @return : success: Pointer to a netgroup entry
 *           failure: NULL (if no such key exists in the dict)
 *
 * Not for external use.
 */
static struct netgroup_entry *
_nge_dict_get (dict_t *dict, const char *ngname)
{
        data_t *ngdata = NULL;

        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_NG, dict, err);
        GF_VALIDATE_OR_GOTO (GF_NG, ngname, err);

        ngdata = dict_get (dict, (char *)ngname);
        if (ngdata)
                return (struct netgroup_entry *)ngdata->data;
err:
        return NULL;
}

/**
 * _nge_dict_insert - Insert a netgroup entry into the dict using
 *                    the netgroup name as the key.
 *
 * @dict   : The dict we are inserting into.
 *
 * @nge    : The data to insert into the dict.
 *
 * @return : nothing
 *
 * Not for external use.
 */
static void
_nge_dict_insert (dict_t *dict, struct netgroup_entry *nge)
{
        data_t *ngdata = NULL;

        GF_VALIDATE_OR_GOTO (GF_NG, dict, err);
        GF_VALIDATE_OR_GOTO (GF_NG, nge, err);

        ngdata = bin_to_data (nge, sizeof (*nge));
        dict_set (dict, nge->netgroup_name, ngdata);
err:
        return;
}

/**
 * _ngh_dict_get - Lookup a netgroup host entry from the dict based
 *                 on the hostname.
 *
 * @dict   : The dict we are looking up from. This function makes the
 *           assumption that the type of underlying data in the dict is of type
 *           struct netgroup_host. The behavior is not defined otherwise.
 *
 * @ngname : Key used to lookup in the dict.
 *
 * @return : success: Pointer to a netgroup host entry
 *           failure: NULL (if no such key exists in the dict)
 *
 * Externally usable.
 */
struct netgroup_host *
ngh_dict_get (dict_t *dict, const char *hostname)
{
        data_t *ngdata = NULL;

        GF_VALIDATE_OR_GOTO (GF_NG, dict, err);
        GF_VALIDATE_OR_GOTO (GF_NG, hostname, err);

        ngdata = dict_get (dict, (char *)hostname);
        if (!ngdata)
                goto err;

        return (struct netgroup_host *)ngdata->data;

err:
        return NULL;
}

/**
 * _ngh_dict_insert - Insert a netgroup host entry into the dict using
 *                    the netgroup name as the key.
 *
 * @dict   : The dict we are inserting into.
 *
 * @nge    : The data to insert into the dict.
 *
 * @return : nothing
 *
 * Not for external use.
 */
static void
_ngh_dict_insert (dict_t *dict, struct netgroup_host *ngh)
{
        data_t *ngdata = NULL;

        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_NG, dict, err);
        GF_VALIDATE_OR_GOTO (GF_NG, ngh, err);

        ngdata = bin_to_data (ngh, sizeof (*ngh));
        dict_set (dict, ngh->hostname, ngdata);
err:
        return;
}

/**
 * _ngh_print - Prints the netgroup host in the
 *              format '(hostname,user,domain)'
 *
 * @ngh    : The netgroup host to print out
 *
 * @return : nothing
 *
 * Not for external use.
 */
static void
_ngh_print (const struct netgroup_host *ngh)
{
        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_NG, ngh, err);

        printf ("(%s,%s,%s)", ngh->hostname, ngh->user ? ngh->user : "",
                ngh->domain ? ngh->domain : "");
err:
        return;
}

/**
 * __nge_print_walk - walk the netgroup entry dict and print each entry
 *                    associated with the key. This function prints
 *                    entries of type 'struct netgroup_entry'.
 *
 * This is passed as a function pointer to dict_foreach ()
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__nge_print_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        if (val)
                _nge_print ((struct netgroup_entry *)val->data);

        return 0;
}

/**
 * __ngh_print_walk - walk the netgroup entry dict and print each entry
 *                    associated with the key. This function prints entries
 *                    of type 'struct netgroup_host'
 *
 * This is passed as a function pointer to dict_foreach (),
 * which is called from _nge_print ().
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__ngh_print_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        if (val)
                _ngh_print ((struct netgroup_host *)val->data);

        return 0;
}

/**
 * _nge_print - Prints the netgroup entry in the
 *              format '<netgroup name> <following entries>'
 *
 * @ngh    : The netgroup entry to print out
 *
 * @return : nothing
 *
 * Not for external use.
 */
static void
_nge_print (const struct netgroup_entry *nge)
{
        /* Validate args */
        GF_VALIDATE_OR_GOTO (GF_NG, nge, err);

        printf ("%s ", nge->netgroup_name);
        if (nge->netgroup_ngs)
                dict_foreach (nge->netgroup_ngs, __nge_print_walk, NULL);

        if (nge->netgroup_hosts)
                dict_foreach (nge->netgroup_hosts, __ngh_print_walk, NULL);

err:
        return;
}

/**
 * __ngf_print_walk - walk through each entry in the netgroups file and print it
 *                    out. This calls helper functions _nge_print () to print
 *                    the netgroup entries.
 *
 * This is passed as a function pointer to dict_foreach (),
 * which is called from ng_file_print ().
 *
 * @dict: the dict we are walking
 * @key : the key we are processing in the dict
 * @val : the corresponding value in the dict
 * @tmp : Pointer to additional data that may be passed in (not used)
 *
 * @return : Nothing
 *
 * Not for external use.
 */
static int
__ngf_print_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        struct netgroup_entry *snge = NULL;

        if (val) {
                snge = (struct netgroup_entry *)val->data;
                _nge_print (snge);
                printf ("\n");
        }
        return 0;
}

/**
 * ng_file_print - Prints the netgroup file in the
 *              format '<netgroup name> <following entries>', etc.
 *              The netgroup file is a dict of netgroup entries
 *              which, in turn is a combination of a other 'sub' netgroup
 *              entries and host entries. This function prints
 *              all of that out by calling the corresponding print functions
 *
 * @ngfile : The netgroup file to print out
 *
 * @return : nothing
 *
 * External facing function.
 *
 * Can be called on any valid 'struct netgroups_file *' type.
 */
void
ng_file_print (const struct netgroups_file *ngfile)
{
     dict_foreach (ngfile->ng_file_dict, __ngf_print_walk, NULL);
}

/**
 * ng_file_get_netgroup - Look up a netgroup entry from the netgroups file
 *                        based on the netgroup name and return a pointer
 *                        to the netgroup entry.
 *
 * @ngfile   : The netgroup file to lookup from.
 * @netgroup : The netgroup name used to lookup from the netgroup file.
 *
 * @return : nothing
 *
 * External facing function.
 *
 * Can be called on any valid 'struct netgroups_file *' type with a valid 'char
 * *' as the lookup key.
 */
struct netgroup_entry *
ng_file_get_netgroup (const struct netgroups_file *ngfile, const char *netgroup)
{
        data_t *ndata = NULL;

        GF_VALIDATE_OR_GOTO (GF_NG, ngfile, err);
        GF_VALIDATE_OR_GOTO (GF_NG, netgroup, err);

        ndata = dict_get (ngfile->ng_file_dict,
                          (char *)netgroup);
        if (!ndata)
                goto err;

        return (struct netgroup_entry *)ndata->data;

err:
        return NULL;
}

/**
 * __check_host_entry_str - Check if the host string which should be
 *                          in the format '(host,user,domain)' is
 *                          valid to be parsed. Currently checks
 *                          if the # of commas is correct and there
 *                          are no spaces in the string, but more
 *                          checks can be added.
 *
 * @host_str : String to check
 * @return   : success: TRUE if valid
 *             failure: FALSE if not
 *
 * Not for external use.
 */
static gf_boolean_t
__check_host_entry_str (const char *host_str)
{
        unsigned int comma_count = 0;
        unsigned int i           = 0;
        gf_boolean_t str_valid   = _gf_true;

        GF_VALIDATE_OR_GOTO (GF_NG, host_str, out);

        for (i = 0; i < strlen (host_str); i++) {
                if (host_str[i] == ',')
                        comma_count++;

                /* Spaces are not allowed in this string. e.g, (a,b,c) is valid
                 * but (a, b,c) is not.
                 */
                if (host_str[i] == ' ') {
                        str_valid = _gf_false;
                        goto out;
                }
        }

        str_valid = (comma_count == 2);
out:
        return str_valid;
}

/**
 * _parse_ng_host - Parse the netgroup host string into a netgroup host struct.
 *                  The netgroup host string is structured as follows:
 *                  (host, user, domain)
 *
 * @ng_str   : String to parse
 * @return   : success: 0 if the parsing succeeded
 *             failure: -EINVAL for bad args, -ENOMEM for allocation errors,
 *                      1 for parsing errors.
 *
 * Not for external use.
 */
static int
_parse_ng_host (char *ng_str, struct netgroup_host **ngh)
{
        struct netgroup_host *ng_host  = NULL;
        unsigned int          parts    = 0;
        char                 *match    = NULL;
        int                   ret      = -EINVAL;

        GF_VALIDATE_OR_GOTO (GF_NG, ng_str, out);
        GF_VALIDATE_OR_GOTO (GF_NG, ngh, out);

        if (!__check_host_entry_str (ng_str)) {
                ret = 1; /* Parse failed */
                goto out;
        }

        ret = parser_set_string (ng_host_parser, ng_str);
        if (ret < 0)
                goto out;

        gf_msg_trace (GF_NG, 0, "parsing host string: %s", ng_str);

        ng_host = _netgroup_host_init ();
        GF_CHECK_ALLOC (ng_host, ret, free_and_out); /* Sets ret to -ENOMEM on
                                                      * failure.
                                                      */
        while ((match = parser_get_next_match (ng_host_parser)) != NULL) {
                gf_msg_trace (GF_NG, 0, "found match: %s (parts=%d)", match,
                              parts);

                switch (parts) {
                case 0:
                        ng_host->hostname = match;
                        break;
                case 1:
                        ng_host->user = match;
                        break;
                case 2:
                        ng_host->domain = match;
                        break;
                default:
                        GF_FREE (match);
                        break;
                };

                /* We only allow three parts in the host string;
                 * The format for the string is (a,b,c)
                 */
                parts++;
                if (parts > 2)
                        break;
        }

        /* Set the parameter */
        *ngh = ng_host;
        ret = 0;

free_and_out:
        parser_unset_string (ng_host_parser);
out:
        return ret;
}

/**
 * _ng_handle_host_part - Parse the host string that looks like this :
 *                        '(dev1763.prn2.facebook.com,,)' into a host
 *                        struct and insert it into the parent netgroup's
 *                        host dict.
 * @match : The host string
 * @ngp   : The parent netgroup
 *
 * @return: success: 0 if parsing succeeded
 *          failure: -EINVAL for bad args, other errors bubbled up
 *                   from _parse_ng_host.
 *
 *
 * Not for external use.
 */
static int
_ng_handle_host_part (char *match, struct netgroup_entry *ngp)
{
        struct netgroup_host *ngh = NULL;
        int                   ret = -EINVAL;

        GF_VALIDATE_OR_GOTO (GF_NG, match, out);
        GF_VALIDATE_OR_GOTO (GF_NG, ngp, out);

        if (!ngp->netgroup_name) {
                gf_msg (GF_NG, GF_LOG_WARNING, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid: Line starts with hostname!");
                goto out;
        }

        /* Parse the host string and get a struct for it */
        ret = _parse_ng_host (match, &ngh);
        if (ret < 0) {
                gf_msg (GF_NG, GF_LOG_CRITICAL, -ret, NFS_MSG_PARSE_FAIL,
                        "Critical error : %s", strerror (-ret));
                goto out;
        }
        if (ret != 0) {
                /* Cannot change to gf_msg
                 * gf_msg not giving output to STDOUT
                 * Bug id : BZ1215017
                 */
                gf_log (GF_NG, GF_LOG_WARNING,
                        "Parse error for: %s", match);
                goto out;
        }


        /* Make dict for the parent entry's netgroup hosts */
        if (!ngp->netgroup_hosts) {
                ngp->netgroup_hosts = dict_new ();
                GF_CHECK_ALLOC (ngp->netgroup_hosts, ret,
                                out);
        }

        /* Insert this entry into the parent netgroup dict */
        _ngh_dict_insert (ngp->netgroup_hosts, ngh);

out:
        return ret;
}

/**
 * _ng_handle_netgroup_part - Parse the netgroup string that should just be one
 *                            string. This may insert the netgroup into the file
 *                            struct if it does not already exist. Frees the
 *                            parameter match if the netgroup was already found
 *                            in the file.
 *
 * @match    : The netgroup string
 * @ngp      : The netgroup file we may insert the entry into
 * @ng_entry : Double pointer to the netgroup entry we want to allocate and set.
 *
 * @return: success: 0 if parsing succeeded
 *          failure: -EINVAL for bad args, other errors bubbled up
 *                   from _parse_ng_host.
 *
 *
 * Not for external use.
 */
static int
_ng_setup_netgroup_entry (char *match, struct netgroups_file *file,
                          struct netgroup_entry **ng_entry)
{
        struct netgroup_entry *nge           = NULL;
        int                    ret           = -EINVAL;

        GF_VALIDATE_OR_GOTO (GF_NG, match, out);
        GF_VALIDATE_OR_GOTO (GF_NG, file, out);
        GF_VALIDATE_OR_GOTO (GF_NG, ng_entry, out);

        nge = _netgroup_entry_init ();
        GF_CHECK_ALLOC (nge, ret, out);

        nge->netgroup_name = match;

        /* Insert this new entry into the file dict */
        _nge_dict_insert (file->ng_file_dict, nge);

        *ng_entry = nge;

        ret = 0;
out:
        return ret;
}

/**
 * _parse_ng_line - Parse a line in the netgroups file into a netgroup entry
 *                  struct. The netgroup line is structured as follows:
 *                  'netgroupx netgroupy (hosta,usera,domaina)...' OR
 *                  'netgroupx netgroupy netgroupz...'  OR
 *                  'netgroupx (hosta,usera,domaina) (hostb,userb,domainb)'
 *                  This function parses this into a netgroup entry
 *                  which will hold either a dict of netgroups and/or
 *                  a dict of hosts that make up this netgroup.
 *
 * In general terms, the data structure to represent a netgroups file
 * is a set of nested dictionaries. Each line in the netgroups file
 * is compiled into a struct netgroup_entry structure that holds a dict
 * of netgroups and a dict of hostnames. The first string in the netgroups
 * line is the parent netgroup entry and the rest of the items in the line
 * are the children of that parent netgroup entry. (Hence variables ngp
 * and nge).
 *
 * A sample netgroup file may look like this:
 *
 * async async.ash3 async.ash4
 * async.ash3 async.04.ash3
 * async04.ash3 (async001.ash3.facebook.com,,) (async002.ash3.facebook.com,,)
 *
 * _parse_ng_line will get called on each line, so on the first call to this
 * function, our data structure looks like this:
 *
 *
 * dict [
 *       'async'   --> dict [
 *                              'async.ash3'
 *                              'async.ash4'
 *                          ]
 *      ]
 *
 * On the second call to the function with the second line, our data structure
 * looks like this:
 *
 * dict [
 *       'async' --> dict [
 *                              'async.ash3' -> dict [ 'async.04.ash3' ]
 *                              'async.ash4'      ^
 *                        ]                       |
 *                                                |
 *      'async.ash3' ------------------------------
 *      ]
 *
 * And so on.
 *
 * The obvious answer to storing this file in a data structure may be a tree
 * but lookups from a tree are expensive and since we may be looking up stuff
 * in this file in the I/O path, we can't afford expensive lookups.
 *
 * @ng_str   : String to parse
 * @file     : Netgroup file to put the parsed line into
 * @ng_entry : Double pointer to struct that we are going to allocate and fill
 *
 * The string gets parsed into a structure pointed to by
 * the parameter 'ng_entry'
 *
 * @return   : success: 0 if parsing succeeded
 *             failure: NULL if not
 *
 * Not for external use.
 */
static int
_parse_ng_line (char *ng_str, struct netgroups_file *file,
                struct netgroup_entry **ng_entry)
{
        struct netgroup_entry  *ngp         = NULL; /* Parent netgroup entry */
        struct netgroup_entry  *nge         = NULL; /* Generic netgroup entry */
        char                   *match       = NULL;
        int                     ret         = -EINVAL;
        unsigned int            num_entries = 0;

        /* Validate arguments */
        GF_VALIDATE_OR_GOTO (GF_NG, ng_str, out);
        GF_VALIDATE_OR_GOTO (GF_NG, file, out);

        if (*ng_str == ' ' || *ng_str == '\0' || *ng_str == '\n') {
                ret = 0;
                goto out;
        }

        ret = parser_set_string (ng_file_parser, ng_str);
        if (ret < 0)
                goto out;

        /* This is the first name in the line, and should be the
         * parent netgroup entry.
         */
        match = parser_get_next_match (ng_file_parser);
        if (!match) {
                ret = 1;
                gf_msg (GF_NG, GF_LOG_WARNING, 0,
                        NFS_MSG_FIND_FIRST_MATCH_FAIL, "Unable to find "
                        "first match.");
                gf_msg (GF_NG, GF_LOG_WARNING, 0, NFS_MSG_PARSE_FAIL,
                        "Error parsing str: %s", ng_str);
                goto out;
        }

        /* Lookup to see if the match already exists,
         * if not, set the parent.
         */
        ngp = _nge_dict_get (file->ng_file_dict, match);
        if (!ngp) {
                ret = _ng_setup_netgroup_entry (match, file, &ngp);
                if (ret < 0) {
                        /* Bubble up error to caller. We don't need to free ngp
                         * here because this can only fail if allocating the
                         * struct fails.
                         */
                        goto out;
                }
        } else
                GF_FREE (match);

        if (!ngp->netgroup_ngs) {
                /* If a netgroup dict has not been allocated
                 * for this parent, allocate it.
                 */
                ngp->netgroup_ngs = dict_new ();
                GF_CHECK_ALLOC (ngp->netgroup_ngs, ret, out);
                /* No need to free anything here since ngp is already
                 * a part of the file. When the file gets
                 * deallocated, we will free ngp.
                 */
        }

        while ((match = parser_get_next_match (ng_file_parser)) != NULL) {
                num_entries++;
                /* This means that we hit a host entry in the line */
                if (*match == '(') {
                        ret = _ng_handle_host_part (match, ngp);
                        GF_FREE (match);
                        if (ret != 0) {
                                /* If parsing the host fails, bubble the error
                                 * code up to the caller.
                                 */
                                goto out;
                        }
                } else {
                        nge = _nge_dict_get (file->ng_file_dict, match);
                        if (!nge) {
                                ret = _ng_setup_netgroup_entry (match, file,
                                                                &nge);
                                if (ret < 0) {
                                        /* Bubble up error to caller. We don't
                                         * need to free nge here because this
                                         * can only fail if allocating the
                                         * struct fails.
                                         */
                                        goto out;
                                }
                        } else
                                GF_FREE (match);

                        /* Insert the netgroup into the parent's dict */
                        _nge_dict_insert (ngp->netgroup_ngs, nge);
                }
        }

        /* If there are no entries on the RHS, log an error, but continue */
        if (!num_entries) {
                /* Cannot change to gf_msg
                 * gf_msg not giving output to STDOUT
                 * Bug id : BZ1215017
                 */
                gf_log (GF_NG, GF_LOG_WARNING,
                        "No netgroups were specified except for the parent.");
        }

        *ng_entry = ngp;
        ret = 0;

out:
        parser_unset_string (ng_file_parser);
        return ret;
}

/**
 * ng_file_parse - Parse a netgroups file into a the netgroups file struct.
 *                 This is the external facing function that must be called
 *                 to parse a netgroups file. This function returns a netgroup
 *                 file struct that is allocated and must be freed using
 *                 ng_file_deinit.
 *
 * @filepath : Path to the netgroups file we need to parse
 *
 * @return   : success: Pointer to a netgroup file struct if parsing succeeded
 *             failure: NULL if not
 *
 * Externally facing function
 */
struct netgroups_file *
ng_file_parse (const char *filepath)
{
        FILE                  *fp    = NULL;
        size_t                 len   = 0;
        size_t                 read  = 0;
        char                  *line  = NULL;
        struct netgroups_file *file  = NULL;
        struct netgroup_entry *nge   = NULL;
        int                    ret   = 0;

        GF_VALIDATE_OR_GOTO (GF_NG, filepath, err);

        fp = fopen (filepath, "r");
        if (!fp)
                goto err;

        file = _netgroups_file_init ();
        if (!file)
                goto err;

        file->ng_file_dict = dict_new ();
        if (!file->ng_file_dict) {
                gf_msg (GF_NG, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate netgroup file dict");
                goto err;
        }

        file->filename = gf_strdup (filepath);
        if (!file->filename) {
                gf_msg (GF_NG, GF_LOG_CRITICAL, errno, NFS_MSG_FILE_OP_FAILED,
                        "Failed to duplicate filename");
                goto err;
        }

        ret = _ng_init_parsers ();
        if (ret < 0)
                goto err;

        /* Read the file line-by-line and parse it */
        while ((read = getline (&line, &len, fp)) != -1) {
                if (*line == '#') /* Lines starting with # are comments */
                        continue;

                /* Parse the line into a netgroup entry */
                ret = _parse_ng_line (line, file, &nge);
                if (ret == -ENOMEM) {
                        gf_msg (GF_NG, GF_LOG_CRITICAL, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Allocation error "
                                "while parsing line!");
                        ng_file_deinit (file);
                        GF_FREE (line);
                        goto err;
                }
                if (ret != 0) {
                        gf_msg_debug (GF_NG, 0, "Failed to parse line %s",
                                      line);
                        continue;
                }
        }

        /* line got allocated through getline(), don't use GF_FREE() for it */
        free (line);

        if (fp)
                fclose(fp);

        return file;

err:
        if (file)
                ng_file_deinit (file);

        _ng_deinit_parsers ();

        if (fp)
                fclose (fp);
        return NULL;
}
