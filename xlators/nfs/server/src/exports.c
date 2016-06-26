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

#include "exports.h"
#include "hashfn.h"
#include "parse-utils.h"
#include "nfs-messages.h"

static void _exp_dict_destroy (dict_t *ng_dict);
static void _export_options_print (const struct export_options *opts);
static void _export_options_deinit (struct export_options *opts);
static void _export_dir_deinit (struct export_dir *dir);

static struct parser *netgroup_parser;
static struct parser *hostname_parser;
static struct parser *options_parser;

/**
 * _exp_init_parsers -- Initialize parsers to be used in this file
 *
 * @return: success: 0
 *          failure: -1
 */
static int
_exp_init_parsers ()
{
        int ret = -1;

        netgroup_parser = parser_init (NETGROUP_REGEX_PATTERN);
        if (!netgroup_parser)
                goto out;

        hostname_parser = parser_init (HOSTNAME_REGEX_PATTERN);
        if (!hostname_parser)
                goto out;

        options_parser = parser_init (OPTIONS_REGEX_PATTERN);
        if (!options_parser)
                goto out;

        ret = 0;
out:
        return ret;
}

/**
 * _exp_deinit_parsers -- Free parsers used in this file
 */
static void
_exp_deinit_parsers ()
{
        parser_deinit (netgroup_parser);
        parser_deinit (hostname_parser);
        parser_deinit (options_parser);
}

/**
 * _export_file_init -- Initialize an exports file structure.
 *
 * @return  : success: Pointer to an allocated exports file struct
 *            failure: NULL
 *
 * Not for external use.
 */
struct exports_file *
_exports_file_init ()
{
        struct exports_file *file = NULL;

        file = GF_CALLOC (1, sizeof (*file), gf_common_mt_nfs_exports);
        if (!file) {
                gf_msg (GF_EXP, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate file struct!");
                goto out;
        }

        file->exports_dict = dict_new ();
        file->exports_map = dict_new ();
        if (!file->exports_dict || !file->exports_map) {
                gf_msg (GF_EXP, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate dict!");
                goto free_and_out;
        }

        goto out;

free_and_out:
        if (file->exports_dict)
                dict_unref (file->exports_dict);

        GF_FREE (file);
        file = NULL;
out:
        return file;
}

/**
 * _exp_file_dict_destroy -- Delete each item in the dict
 *
 * @dict : Dict to free elements from
 * @key  : Key in the dict we are on
 * @val  : Value associated with that dict
 * @tmp  : Not used
 *
 * Not for external use.
 */
static int
_exp_file_dict_destroy (dict_t *dict, char *key, data_t *val, void *tmp)
{
        struct export_dir *dir = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, dict, out);

        if (val) {
                dir = (struct export_dir *)val->data;

                if (dir) {
                        _export_dir_deinit (dir);
                        val->data = NULL;
                }
                dict_del (dict, key);
        }

out:
        return 0;
}

/**
 * _exp_file_deinit -- Free memory used by an export file
 *
 * @expfile : Pointer to the exports file to free
 *
 * Externally usable.
 */
void
exp_file_deinit (struct exports_file *expfile)
{
        if (!expfile)
                goto out;

        if (expfile->exports_dict) {
                dict_foreach (expfile->exports_dict, _exp_file_dict_destroy,
                              NULL);
                dict_unref (expfile->exports_dict);
        }

        if (expfile->exports_map) {
                dict_foreach (expfile->exports_map, _exp_file_dict_destroy,
                              NULL);
                dict_unref (expfile->exports_map);
        }

        GF_FREE (expfile->filename);
        GF_FREE (expfile);
out:
        return;
}

/**
 * _export_dir_init -- Initialize an export directory structure.
 *
 * @return  : success: Pointer to an allocated exports directory struct
 *            failure: NULL
 *
 * Not for external use.
 */
static struct export_dir *
_export_dir_init ()
{
        struct export_dir *expdir  = GF_CALLOC (1, sizeof (*expdir),
                                                gf_common_mt_nfs_exports);

        if (!expdir)
                gf_msg (GF_EXP, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate export dir structure!");

        return expdir;
}

/**
 * _export_dir_deinit -- Free memory used by an export dir
 *
 * @expdir : Pointer to the export directory to free
 *
 * Not for external use.
 */
static void
_export_dir_deinit (struct export_dir *dir)
{
        GF_VALIDATE_OR_GOTO (GF_EXP, dir, out);
        GF_FREE (dir->dir_name);
        _exp_dict_destroy (dir->netgroups);
        _exp_dict_destroy (dir->hosts);
        GF_FREE (dir);

out:
        return;
}


/**
 * _export_item_print -- Print the elements in the export item.
 *
 * @expdir : Pointer to the item struct to print out.
 *
 * Not for external use.
 */
static void
_export_item_print (const struct export_item *item)
{
        GF_VALIDATE_OR_GOTO (GF_EXP, item, out);
        printf ("%s", item->name);
        _export_options_print (item->opts);
out:
        return;
}

/**
 * _export_item_init -- Initialize an export item structure
 *
 * @return  : success: Pointer to an allocated exports item struct
 *            failure: NULL
 *
 * Not for external use.
 */
static struct export_item *
_export_item_init ()
{
        struct export_item *item = GF_CALLOC (1, sizeof (*item),
                                              gf_common_mt_nfs_exports);

        if (!item)
                gf_msg (GF_EXP, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate export item!");

        return item;
}

/**
 * _export_item_deinit -- Free memory used by an export item
 *
 * @expdir : Pointer to the export item to free
 *
 * Not for external use.
 */
static void
_export_item_deinit (struct export_item *item)
{
        if (!item)
                return;

        _export_options_deinit (item->opts);
        GF_FREE (item->name);
        GF_FREE (item);
}

/**
 * _export_host_init -- Initialize an export options struct
 *
 * @return  : success: Pointer to an allocated options struct
 *            failure: NULL
 *
 * Not for external use.
 */
static struct export_options *
_export_options_init ()
{
        struct export_options *opts = GF_CALLOC (1, sizeof (*opts),
                                                 gf_common_mt_nfs_exports);

        if (!opts)
                gf_msg (GF_EXP, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate options structure!");

        return opts;
}

/**
 * _export_options_deinit -- Free memory used by a options struct
 *
 * @expdir : Pointer to the options struct to free
 *
 * Not for external use.
 */
static void
_export_options_deinit (struct export_options *opts)
{
        if (!opts)
                return;

        GF_FREE (opts->anon_uid);
        GF_FREE (opts->sec_type);
        GF_FREE (opts);
}

/**
 * _export_options_print -- Print the elements in the options struct.
 *
 * @expdir : Pointer to the options struct to print out.
 *
 * Not for external use.
 */
static void
_export_options_print (const struct export_options *opts)
{
        GF_VALIDATE_OR_GOTO (GF_EXP, opts, out);

        printf ("(");
        if (opts->rw)
                printf ("rw,");
        else
                printf ("ro,");

        if (opts->nosuid)
                printf ("nosuid,");

        if (opts->root)
                printf ("root,");

        if (opts->anon_uid)
                printf ("anonuid=%s,", opts->anon_uid);

        if (opts->sec_type)
                printf ("sec=%s,", opts->sec_type);

        printf (") ");
out:
        return;
}

/**
 * __exp_dict_free_walk -- Delete each item in the dict
 *
 * @dict : Dict to free elements from
 * @key  : Key in the dict we are on
 * @val  : Value associated with that dict
 * @tmp  : Not used
 *
 * Passed as a function pointer to dict_foreach()
 *
 * Not for external use.
 */
static int
__exp_dict_free_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        if (val) {
                _export_item_deinit ((struct export_item *)val->data);
                val->data = NULL;
                dict_del (dict, key);
        }
        return 0;
}

/**
 * _exp_dict_destroy -- Delete all the items from this dict
 *                               through the helper function above.
 *
 * @ng_dict : Dict to free
 *
 * Not for external use.
 */
static void
_exp_dict_destroy (dict_t *ng_dict)
{
        if (!ng_dict)
                goto out;

        dict_foreach (ng_dict, __exp_dict_free_walk, NULL);
out:
        return;
}

/**
 * exp_file_dir_from_uuid -- Using a uuid as the key, retrieve an exports
 *                           directory from the file.
 *
 * @file: File to retrieve data from
 * @export_uuid: UUID of the export (mountid in the NFS xlator)
 *
 * @return : success: Pointer to an export dir struct
 *           failure: NULL
 */
struct export_dir *
exp_file_dir_from_uuid (const struct exports_file *file,
                        const uuid_t export_uuid)
{
        char               export_uuid_str[512] = {0, };
        data_t            *dirdata              = NULL;
        struct export_dir *dir                  = NULL;

        gf_uuid_unparse (export_uuid, export_uuid_str);

        dirdata = dict_get (file->exports_map, export_uuid_str);
        if (dirdata)
                dir = (struct export_dir *)dirdata->data;

        return dir;
}

/**
 * _exp_file_insert -- Insert the exports directory into the file structure
 *                     using the directory as a dict. Also hashes the dirname,
 *                     stores it in a uuid type, converts the uuid type to a
 *                     string and uses that as the key to the exports map.
 *                     The exports map maps an export "uuid" to an export
 *                     directory struct.
 *
 * @file : Exports file struct to insert into
 * @dir  : Export directory to insert
 *
 * Not for external use.
 */
static void
_exp_file_insert (struct exports_file *file, struct export_dir *dir)
{
        data_t   *dirdata              = NULL;
        uint32_t  hashedval            = 0;
        uuid_t    export_uuid          = {0, };
        char      export_uuid_str[512] = {0, };
        char     *dirdup               = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, file, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, dir, out);

        dirdata = bin_to_data (dir, sizeof (*dir));
        dict_set (file->exports_dict, dir->dir_name, dirdata);

        dirdup = strdupa (dir->dir_name);
        while (strlen (dirdup) > 0 && dirdup[0] == '/')
                dirdup++;

        hashedval = SuperFastHash (dirdup, strlen (dirdup));
        memset (export_uuid, 0, sizeof (export_uuid));
        memcpy (export_uuid, &hashedval, sizeof (hashedval));
        gf_uuid_unparse (export_uuid, export_uuid_str);

        dict_set (file->exports_map, export_uuid_str, dirdata);
out:
        return;
}

/**
 * __exp_item_print_walk -- Print all the keys and values in the dict
 *
 * @dict : the dict to walk
 * @key  : the key in the dict we are currently on
 * @val  : the value in the dict assocated with the key
 * @tmp  : Additional parameter data (not used)
 *
 * Passed as a function pointer to dict_foreach ().
 *
 * Not for external use.
 */
static int
__exp_item_print_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        if (val)
                _export_item_print ((struct export_item *)val->data);

        return 0;
}

/**
 * __exp_file_print_walk -- Print all the keys and values in the dict
 *
 * @dict : the dict to walk
 * @key  : the key in the dict we are currently on
 * @val  : the value in the dict assocated with the key
 * @tmp  : Additional parameter data (not used)
 *
 * Passed as a function pointer to dict_foreach ().
 *
 * Not for external use.
 */
static int
__exp_file_print_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        if (val) {
                struct export_dir *dir = (struct export_dir *)val->data;

                printf ("%s ", key);

                if (dir->netgroups)
                        dict_foreach (dir->netgroups, __exp_item_print_walk,
                                      NULL);

                if (dir->hosts)
                        dict_foreach (dir->hosts, __exp_item_print_walk, NULL);

                printf ("\n");
        }
        return 0;
}

/**
 * exp_file_print --  Print out the contents of the exports file
 *
 * @file : Exports file to print
 *
 * Not for external use.
 */
void
exp_file_print (const struct exports_file *file)
{
        GF_VALIDATE_OR_GOTO (GF_EXP, file, out);
        dict_foreach (file->exports_dict, __exp_file_print_walk, NULL);
out:
        return;
}

#define __exp_line_get_opt_val(val, equals, ret, errlabel)      \
        do {                                                    \
                (val) = (equals) + 1;                           \
                if (!(*(val))) {                                \
                        (ret) = 1;                              \
                        goto errlabel;                          \
                }                                               \
        } while (0)                                             \

enum gf_exp_parse_status {
        GF_EXP_PARSE_SUCCESS                 = 0,
        GF_EXP_PARSE_ITEM_NOT_FOUND          = 1,
        GF_EXP_PARSE_ITEM_FAILURE            = 2,
        GF_EXP_PARSE_ITEM_NOT_IN_MOUNT_STATE = 3,
        GF_EXP_PARSE_LINE_IGNORING           = 4,
};

/**
 * __exp_line_opt_key_value_parse -- Parse the key-value options in the options
 *                                   string.
 *
 * Given a string like (sec=sys,anonuid=0,rw), to parse, this function
 * will get called once with 'sec=sys' and again with 'anonuid=0'.
 * It will check for the '=', make sure there is data to be read
 * after the '=' and copy the data into the options struct.
 *
 * @option    : An option string like sec=sys or anonuid=0
 * @opts      : Pointer to an struct export_options that holds all the export
 *              options.
 *
 * @return: success: GF_EXP_PARSE_SUCCESS
 *          failure: GF_EXP_PARSE_ITEM_FAILURE on parse failure,
 *                   -EINVAL on bad args, -ENOMEM on allocation errors.
 *
 * Not for external use.
 */
static int
__exp_line_opt_key_value_parse (char *option, struct export_options *opts)
{
        char              *equals   = NULL;
        char              *right    = NULL;
        char              *strmatch = option;
        int                ret      = -EINVAL;

        GF_VALIDATE_OR_GOTO (GF_EXP, option, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, opts, out);

        equals = strchr (option, '=');
        if (!equals) {
                ret = GF_EXP_PARSE_ITEM_FAILURE;
                goto out;
        }

        *equals = 0;
        /* Now that an '=' has been found the left side is the option and
         * the right side is the value. We simply have to compare those and
         * extract it.
         */
        if (strcmp (strmatch, "anonuid") == 0) {
                *equals = '=';
                /* Get the value for this option */
                __exp_line_get_opt_val (right, equals, ret, out);
                opts->anon_uid = gf_strdup (right);
                GF_CHECK_ALLOC (opts->anon_uid, ret, out);
        } else if (strcmp (strmatch, "sec") == 0) {
                *equals = '=';
                /* Get the value for this option */
                __exp_line_get_opt_val (right, equals, ret, out);
                opts->sec_type = gf_strdup (right);
                GF_CHECK_ALLOC (opts->sec_type, ret, out);
        } else {
                *equals = '=';
                ret = GF_EXP_PARSE_ITEM_FAILURE;
                goto out;
        }

        ret = GF_EXP_PARSE_SUCCESS;
out:
        return ret;
}

/**
 * __exp_line_opt_parse -- Parse the options part of an
 *                          exports or netgroups string.
 *
 * @opt_str     : The option string to parse
 * @exp_opts    : Double pointer to the options we are going
 *                to allocate and setup.
 *
 *
 * @return: success: GF_EXP_PARSE_SUCCESS
 *          failure: GF_EXP_PARSE_ITEM_FAILURE on parse failure,
 *                   -EINVAL on bad args, -ENOMEM on allocation errors.
 *
 * Not for external use.
 */
static int
__exp_line_opt_parse (const char *opt_str, struct export_options **exp_opts)
{
        struct export_options  *opts     = NULL;
        char                   *strmatch = NULL;
        int                    ret       = -EINVAL;
        char                   *equals   = NULL;

        ret = parser_set_string (options_parser, opt_str);
        if (ret < 0)
                goto out;

        while ((strmatch = parser_get_next_match (options_parser))) {
                if (!opts) {
                        /* If the options have not been allocated,
                         * allocate it.
                         */
                        opts = _export_options_init ();
                        if (!opts) {
                                ret = -ENOMEM;
                                parser_unset_string (options_parser);
                                goto out;
                        }
                }

                /* First,  check for all the boolean options Second, check for
                 * an '=', and check the available options there. The string
                 * parsing here gets slightly messy, but the concept itself
                 * is pretty simple.
                 */
                equals = strchr (strmatch, '=');
                if (strcmp (strmatch, "root") == 0)
                        opts->root = _gf_true;
                else if (strcmp (strmatch, "ro") == 0)
                        opts->rw = _gf_false;
                else if (strcmp (strmatch, "rw") == 0)
                        opts->rw = _gf_true;
                else if (strcmp (strmatch, "nosuid") == 0)
                        opts->nosuid = _gf_true;
                else if (equals) {
                        ret = __exp_line_opt_key_value_parse (strmatch, opts);
                        if (ret < 0) {
                                /* This means invalid key value options were
                                 * specified, or memory allocation failed.
                                 * The ret value gets bubbled up to the caller.
                                 */
                                GF_FREE (strmatch);
                                parser_unset_string (options_parser);
                                _export_options_deinit (opts);
                                goto out;
                        }
                } else
                        /* Cannot change to gf_msg.
                         * gf_msg not giving output to STDOUT
                         * Bug id : BZ1215017
                         */
                        gf_log (GF_EXP, GF_LOG_WARNING,
                                "Could not find any valid options for "
                                "string: %s", strmatch);
                GF_FREE (strmatch);
        }

        if (!opts) {
                /* If opts is not allocated
                 * that means no matches were found
                 * which is a parse error. Not marking
                 * it as "not found" because it is a parse
                 * error to not have options.
                 */
                ret = GF_EXP_PARSE_ITEM_FAILURE;
                parser_unset_string (options_parser);
                goto out;
        }

        *exp_opts = opts;
        parser_unset_string (options_parser);
        ret = GF_EXP_PARSE_SUCCESS;
out:
        return ret;
}


/**
 * __exp_line_ng_host_str_parse -- Parse the netgroup or host string
 *
 *      e.g. @mygroup(<options>), parsing @mygroup and (<options>)
 *      or   myhost001.dom(<options>), parsing myhost001.dom and (<options>)
 *
 * @line      : The line to parse
 * @exp_item  : Double pointer to a struct export_item
 *
 * @return: success: GF_PARSE_SUCCESS
 *          failure: GF_EXP_PARSE_ITEM_FAILURE on parse failure,
 *                   -EINVAL on bad args, -ENOMEM on allocation errors.
 *
 * Not for external use.
 */
static int
__exp_line_ng_host_str_parse (char *str, struct export_item **exp_item)
{
        struct export_item     *item      = NULL;
        int                     ret       = -EINVAL;
        char                   *parens    = NULL;
        char                   *optstr    = NULL;
        struct export_options  *exp_opts  = NULL;
        char                   *item_name = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, str, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, exp_item, out);

        /* A netgroup/host string looks like this:
         * @test(sec=sys,rw,anonuid=0) or host(sec=sys,rw,anonuid=0)
         * We want to extract the name, 'test' or 'host'
         * Again, we could setup a regex and use it here,
         * but its simpler to find the '(' and copy until
         * there.
         */
        parens = strchr (str, '(');
        if (!parens) {
                /* Parse error if there are no parens. */
                ret = GF_EXP_PARSE_ITEM_FAILURE;
                goto out;
        }

        *parens = '\0'; /* Temporarily terminate it so we can do a copy */

        if (strlen (str) > FQDN_MAX_LEN) {
                ret = GF_EXP_PARSE_ITEM_FAILURE;
                goto out;
        }

        /* Strip leading whitespaces */
        while (*str == ' ' || *str == '\t')
                str++;

        item_name = gf_strdup (str);
        GF_CHECK_ALLOC (item_name, ret, out);

        gf_msg_trace (GF_EXP, 0, "found hostname/netgroup: %s", item_name);

        /* Initialize an export item for this */
        item = _export_item_init ();
        GF_CHECK_ALLOC (item, ret, free_and_out);
        item->name = item_name;

        *parens = '('; /* Restore the string */

        /* Options start at the parantheses */
        optstr = parens;

        ret = __exp_line_opt_parse (optstr, &exp_opts);
        if (ret != 0) {
                /* Bubble up the error to the caller */
                _export_item_deinit (item);
                goto out;
        }

        item->opts = exp_opts;

        *exp_item = item;

        ret = GF_EXP_PARSE_SUCCESS;
        goto out;

free_and_out:
        GF_FREE (item_name);
out:
        return ret;
}

/**
 * __exp_line_ng_parse -- Extract the netgroups in the line
 *                        and call helper functions to parse
 *                        the string.
 *
 * The call chain goes like this:
 *
 * 1) __exp_line_ng_parse ("/test  @test(sec=sys,rw,anonuid=0)")
 * 2) __exp_line_ng_str_parse ("@test(sec=sys,rw,anonuid=0)");
 * 3) __exp_line_opt_parse("(sec=sys,rw,anonuid=0)");
 *
 *
 * @line    : The line to parse
 * @ng_dict : Double pointer to the dict we want to
 *            insert netgroups into.
 *
 * Allocates the dict, extracts netgroup strings from the line,
 * parses them into a struct export_item structure and inserts
 * them in the dict.
 *
 * @return: success: GF_EXP_PARSE_SUCCESS
 *          failure: GF_EXP_PARSE_ITEM_FAILURE on parse failure,
 *                   GF_EXP_PARSE_ITEM_NOT_FOUND if the netgroup was not found
 *                   -EINVAL on bad args, -ENOMEM on allocation errors.
 *
 * Not for external use.
 */
static int
__exp_line_ng_parse (const char *line, dict_t **ng_dict)
{
        dict_t                  *netgroups = NULL;
        char                    *strmatch  = NULL;
        int                      ret       = -EINVAL;
        struct export_item      *exp_ng    = NULL;
        data_t                  *ngdata    = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, line, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, ng_dict, out);

        *ng_dict = NULL; /* Will be set if parse is successful */

        /* Initialize a parser with the line to parse
         * and the regex used to parse it.
         */
        ret = parser_set_string (netgroup_parser, line);
        if (ret < 0) {
                goto out;
        }

        gf_msg_trace (GF_EXP, 0, "parsing line: %s", line);

        while ((strmatch = parser_get_next_match (netgroup_parser))) {
                if (!netgroups) {
                        /* Allocate a new dict to store the netgroups. */
                        netgroups = dict_new ();
                        if (!netgroups) {
                                ret = -ENOMEM;
                                goto free_and_out;
                        }
                }

                gf_msg_trace (GF_EXP, 0, "parsing netgroup: %s", strmatch);

                ret = __exp_line_ng_host_str_parse (strmatch, &exp_ng);

                if (ret != 0) {
                        /* Parsing or other critical errors.
                         * caller will handle return value.
                         */
                        _exp_dict_destroy (netgroups);
                        goto free_and_out;
                }

                ngdata = bin_to_data (exp_ng, sizeof (*exp_ng));
                dict_set (netgroups, exp_ng->name, ngdata);

                /* Free this matched string and continue parsing. */
                GF_FREE (strmatch);
        }

        /* If the netgroups dict was not allocated, then we know that
         * no matches were found.
         */
        if (!netgroups) {
                ret = GF_EXP_PARSE_ITEM_NOT_FOUND;
                parser_unset_string (netgroup_parser);
                goto out;
        }

        ret = GF_EXP_PARSE_SUCCESS;
        *ng_dict = netgroups;

free_and_out:
        parser_unset_string (netgroup_parser);
        GF_FREE (strmatch);
out:
        return ret;
}

/**
 * __exp_line_host_parse -- Extract the hosts in the line
 *                          and call helper functions to parse
 *                          the string.
 *
 * The call chain goes like this:
 *
 * 1) __exp_line_host_parse ("/test  hostip(sec=sys,rw,anonuid=0)")
 * 2) __exp_line_ng_host_str_parse ("hostip(sec=sys,rw,anonuid=0)");
 * 3) __exp_line_opt_parse("(sec=sys,rw,anonuid=0)");
 *
 *
 * @line    : The line to parse
 * @ng_dict : Double pointer to the dict we want to
 *            insert hosts into.
 *
 * Allocates the dict, extracts host strings from the line,
 * parses them into a struct export_item structure and inserts
 * them in the dict.
 *
 * @return: success: GF_EXP_PARSE_SUCCESS
 *          failure: GF_EXP_PARSE_ITEM_FAILURE on parse failure,
 *                   GF_EXP_PARSE_ITEM_NOT_FOUND if the host was not found,
 *                   -EINVAL on bad args, -ENOMEM on allocation errors.
 *
 * Not for external use.
 */
static int
__exp_line_host_parse (const char *line, dict_t **host_dict)
{
        dict_t                  *hosts    = NULL;
        char                    *strmatch = NULL;
        int                     ret       = -EINVAL;
        struct export_item      *exp_host = NULL;
        data_t                  *hostdata = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, line, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, host_dict, out);

        *host_dict = NULL;

        /* Initialize a parser with the line to parse and the regex used to
         * parse it.
         */
        ret = parser_set_string (hostname_parser, line);
        if (ret < 0) {
                goto out;
        }

        gf_msg_trace (GF_EXP, 0, "parsing line: %s", line);

        while ((strmatch = parser_get_next_match (hostname_parser))) {
                if (!hosts) {
                        /* Allocate a new dictto store the netgroups. */
                        hosts = dict_new ();
                        GF_CHECK_ALLOC (hosts, ret, free_and_out);
                }

                gf_msg_trace (GF_EXP, 0, "parsing hostname: %s", strmatch);

                ret = __exp_line_ng_host_str_parse (strmatch, &exp_host);

                if (ret != 0) {
                        /* Parsing or other critical error, free allocated
                         * memory and exit. The caller will handle the errors.
                         */
                        _exp_dict_destroy (hosts);
                        goto free_and_out;
                }

                /* Insert export item structure into the hosts dict. */
                hostdata = bin_to_data (exp_host, sizeof (*exp_host));
                dict_set (hosts, exp_host->name, hostdata);


                /* Free this matched string and continue parsing.*/
                GF_FREE (strmatch);
        }

        /* If the hosts dict was not allocated, then we know that
         * no matches were found.
         */
        if (!exp_host) {
                ret = GF_EXP_PARSE_ITEM_NOT_FOUND;
                parser_unset_string (hostname_parser);
                goto out;
        }

        ret = GF_EXP_PARSE_SUCCESS;
        *host_dict = hosts;

free_and_out:
        parser_unset_string (hostname_parser);
        GF_FREE (strmatch);
out:
        return ret;
}


/**
 * __exp_line_dir_parse -- Extract directory name from a line in the exports
 *                         file.
 *
 * @line    : The line to parse
 * @dirname : Double pointer to the string we need to hold the directory name.
 *            If the parsing failed, the string will point to NULL, otherwise
 *            it will point to a valid memory region that is allocated by
 *            this function.
 * @check_ms: If this variable is set then we cross check the directory line
 *            with whats in gluster's vol files and reject them if they don't
 *            match.
 *
 * @return : success: GF_EXP_PARSE_SUCCESS
 *           failure: GF_EXP_PARSE_ITEM_FAILURE on parse failure,
 *           -EINVAL on bad arguments, -ENOMEM on allocation failures,
 *           GF_EXP_PARSE_ITEM_NOT_IN_MOUNT_STATE if we failed to match
 *           with gluster's mountstate.
 *
 * The caller is responsible for freeing memory allocated by this function
 *
 * Not for external use.
 */
static int
__exp_line_dir_parse (const char *line, char **dirname, struct mount3_state *ms)
{
        char                    *dir        = NULL;
        char                    *delim      = NULL;
        int                     ret         = -EINVAL;
        char                    *linedup    = NULL;
        struct mnt3_export      *mnt3export = NULL;
        size_t                  dirlen      = 0;

        GF_VALIDATE_OR_GOTO (GF_EXP, line, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, dirname, out);

        /* Duplicate the line because we don't
         * want to modify the original string.
         */
        linedup = strdupa (line);

        /* We use strtok_r () here to split the string by space/tab and get the
         * the result. We only need the first result of the split.
         * a simple task. It is worth noting that dirnames always have to be
         * validated against gluster's vol files so if they don't
         * match it will be rejected.
         */
        dir = linedup;
        delim = linedup + strcspn (linedup, " \t");
        *delim = 0;

        if (ms) {
                /* Match the directory name with an existing
                 * export in the mount state.
                 */
                mnt3export = mnt3_mntpath_to_export (ms, dir, _gf_true);
                if (!mnt3export) {
                        gf_msg_debug (GF_EXP, 0, "%s not in mount state, "
                                      "ignoring!", dir);
                        ret = GF_EXP_PARSE_ITEM_NOT_IN_MOUNT_STATE;
                        goto out;
                }
        }

        /* Directories can be 1024 bytes in length, check
         * that the argument provided adheres to
         * that restriction.
         */
        if (strlen (dir) > DIR_MAX_LEN) {
                ret = -EINVAL;
                goto out;
        }

        /* Copy the result of the split */
        dir = gf_strdup (dir);
        GF_CHECK_ALLOC (dir, ret, out);

        /* Ensure that trailing slashes are stripped before storing the name */
        dirlen = strlen (dir);
        if (dirlen > 0 && dir[dirlen - 1] == '/')
                dir[dirlen - 1] = '\0';


        /* Set the argument to point to the allocated string */
        *dirname = dir;
        ret = GF_EXP_PARSE_SUCCESS;
out:
        return ret;
}

/**
 * _exp_line_parse -- Parse a line in an exports file into a structure
 *                    that holds all the parts of the line. An exports
 *                    structure has a dict of netgroups and a dict of hosts.
 *
 * An export line looks something like this /test  @test(sec=sys,rw,anonuid=0)
 * or /test  @test(sec=sys,rw,anonuid=0) hostA(sec=sys,rw,anonuid=0), etc.
 *
 * We use regexes to parse the line into three separate pieces:
 * 1) The directory (exports.h -- DIRECTORY_REGEX_PATTERN)
 * 2) The netgroup if it exists (exports.h -- NETGROUP_REGEX_PATTERN)
 * 3) The host if it exists (exports.h -- HOST_REGEX_PATTERN)
 *
 * In this case, the netgroup would be @test(sec=sys,rw,anonuid=0)
 * and the host would be hostA(sec=sys,rw,anonuid=0).
 *
 * @line        : The line to parse
 * @dir         : Double pointer to the struct we need to parse the line into.
 *                If the parsing failed, the struct will point to NULL,
 *                otherwise it will point to a valid memory region that is
 *                allocated by this function.
 * @parse_full  : This parameter tells us whether we should parse all the lines
 *                in the file, even if they are not present in gluster's config.
 *                The gluster config holds the volumes that it exports so
 *                if parse_full is set to FALSE then we will ensure that
 *                the export file structure holds only those volumes
 *                that gluster has exported. It is important to note that
 *                If gluster exports a volume named '/test', '/test' and all
 *                of its subdirectories that may be in the exports file
 *                are valid exports.
 *  @ms         : The mount state that holds the list of volumes that gluster
 *                currently exports.
 *
 * @return : success: GF_EXP_PARSE_SUCCESS on success, -EINVAL on bad arguments,
 *                    -ENOMEM on memory allocation errors,
 *                    GF_EXP_PARSE_LINE_IGNORING if we ignored the line,
 *                    GF_EXP_PARSE_ITEM_FAILURE if there was error parsing
 *           failure: NULL
 *
 * The caller is responsible for freeing memory allocated by this function
 * The caller should free this memory using the _exp_dir_deinit () function.
 *
 * Not for external use.
 */
static int
_exp_line_parse (const char *line, struct export_dir **dir,
                 gf_boolean_t parse_full, struct mount3_state *ms)
{
        struct export_dir *expdir           = NULL;
        char              *dirstr           = NULL;
        dict_t            *netgroups        = NULL;
        dict_t            *hosts            = NULL;
        int                ret              = -EINVAL;
        gf_boolean_t       netgroups_failed = _gf_false;

        GF_VALIDATE_OR_GOTO (GF_EXP, line, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, dir, out);

        if (*line == '#' || *line == ' ' || *line == '\t'
            || *line == '\0' || *line == '\n') {
                ret = GF_EXP_PARSE_LINE_IGNORING;
                goto out;
        }

        expdir = _export_dir_init ();
        if (!expdir) {
                *dir = NULL;
                ret = -ENOMEM;
                goto out;
        }

        /* Get the directory string from the line */
        ret = __exp_line_dir_parse (line, &dirstr, ms);
        if (ret < 0) {
                gf_msg (GF_EXP, GF_LOG_ERROR, 0, NFS_MSG_PARSE_DIR_FAIL,
                        "Parsing directory failed: %s", strerror (-ret));
                /* If parsing the directory failed,
                 * we should simply fail because there's
                 * nothing else we can extract from the string to make
                 * the data valuable.
                 */
                goto free_and_out;
        }

        /* Set the dir str */
        expdir->dir_name = dirstr;

        /* Parse the netgroup part of the string */
        ret = __exp_line_ng_parse (line, &netgroups);
        if (ret < 0) {
                gf_msg (GF_EXP, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                        "Critical error: %s", strerror (-ret));
                /* Return values less than 0
                 * indicate critical failures (null parameters,
                 * failure to allocate memory, etc).
                 */
                goto free_and_out;
        }
        if (ret != 0) {
                if (ret == GF_EXP_PARSE_ITEM_FAILURE)
                        /* Cannot change to gf_msg.
                         * gf_msg not giving output to STDOUT
                         * Bug id : BZ1215017
                         */
                        gf_log (GF_EXP, GF_LOG_WARNING,
                                "Error parsing netgroups for: %s", line);
                /* Even though parsing failed for the netgroups we should let
                 * host parsing proceed.
                 */
                netgroups_failed = _gf_true;
        }

        /* Parse the host part of the string */
        ret = __exp_line_host_parse (line, &hosts);
        if (ret < 0) {
                gf_msg (GF_EXP, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                        "Critical error: %s", strerror (-ret));
                goto free_and_out;
        }
        if (ret != 0) {
                if (ret == GF_EXP_PARSE_ITEM_FAILURE)
                        gf_msg (GF_EXP, GF_LOG_WARNING, 0, NFS_MSG_PARSE_FAIL,
                                "Error parsing hosts for: %s", line);
                /* If netgroups parsing failed, AND
                 * host parsing failed, then theres something really
                 * wrong with this line, so we're just going to
                 * log it and fail out.
                 */
                if (netgroups_failed)
                        goto free_and_out;
        }

        expdir->hosts = hosts;
        expdir->netgroups = netgroups;
        *dir = expdir;
        goto out;

free_and_out:
       _export_dir_deinit (expdir);
out:
        return ret;
}

struct export_item *
exp_dir_get_netgroup (const struct export_dir *expdir, const char *netgroup)
{
        struct export_item   *lookup_res = NULL;
        data_t               *dict_res   = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, expdir, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, netgroup, out);

        if (!expdir->netgroups)
                goto out;

        dict_res = dict_get (expdir->netgroups, (char *)netgroup);
        if (!dict_res) {
                gf_msg_debug (GF_EXP, 0, "%s not found for %s",
                              netgroup, expdir->dir_name);
                goto out;
        }

        lookup_res = (struct export_item *)dict_res->data;
out:
        return lookup_res;
}
/**
 * exp_dir_get_host -- Given a host string and an exports directory structure,
 *                     find and return an struct export_item structure that
 *                     represents the requested host.
 *
 * @expdir: Export directory to lookup from
 * @host  : Host string to lookup
 *
 * @return: success: Pointer to a export item structure
 *          failure: NULL
 */
struct export_item *
exp_dir_get_host (const struct export_dir *expdir, const char *host)
{
        struct export_item   *lookup_res = NULL;
        data_t               *dict_res   = NULL;

        GF_VALIDATE_OR_GOTO (GF_EXP, expdir, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, host, out);

        if (!expdir->hosts)
                goto out;

        dict_res = dict_get (expdir->hosts, (char *)host);
        if (!dict_res) {
                gf_msg_debug (GF_EXP, 0, "%s not found for %s",
                              host, expdir->dir_name);

                /* Check if wildcards are allowed for the host */
                dict_res = dict_get (expdir->hosts, "*");
                if (!dict_res) {
                        goto out;
                }
        }

        lookup_res = (struct export_item *)dict_res->data;
out:
        return lookup_res;
}


/**
 * exp_file_get_dir -- Return an export dir given a directory name
 *                     Does a lookup from the dict in the file structure.
 *
 * @file : Exports file structure to lookup from
 * @dir  : Directory name to lookup
 *
 * @return : success: Pointer to an export directory structure
 *           failure: NULL
 */
struct export_dir *
exp_file_get_dir (const struct exports_file *file, const char *dir)
{
        struct export_dir  *lookup_res = NULL;
        data_t             *dict_res   = NULL;
        char               *dirdup     = NULL;
        size_t             dirlen      = 0;

        GF_VALIDATE_OR_GOTO (GF_EXP, file, out);
        GF_VALIDATE_OR_GOTO (GF_EXP, dir, out);

        dirlen = strlen (dir);
        if (dirlen <= 0)
                goto out;

        dirdup = (char *)dir; /* Point at the directory */

        /* If directories don't contain a leading slash */
        if (*dir != '/') {
                dirdup = alloca (dirlen + 2); /* Leading slash & null byte */
                snprintf (dirdup, dirlen + 2, "/%s", dir);
        }

        dict_res = dict_get (file->exports_dict, dirdup);
        if (!dict_res) {
                gf_msg_debug (GF_EXP, 0, "%s not found in %s", dirdup,
                              file->filename);
                goto out;
        }

        lookup_res = (struct export_dir *)dict_res->data;
out:
        return lookup_res;
}

/**
 * exp_file_parse -- Parse an exports file into a structure
 *                   that can be looked up through simple
 *                   function calls.
 *
 * @filepath: Path to the exports file
 * @ms      : Current mount state (useful to match with gluster vol files)
 *
 * @return  : success: 0
 *            failure: -1 on parsing failure, -EINVAL on bad arguments,
 *                     -ENOMEM on allocation failures.
 *
 * The caller is responsible for freeing memory allocated by this function.
 * The caller should free this memory using the exp_file_deinit () function.
 * Calling GF_FREE ( ) on the pointer will NOT free all the allocated memory.
 *
 * Externally usable.
 */
int
exp_file_parse (const char *filepath, struct exports_file **expfile,
                struct mount3_state *ms)
{
        FILE                *fp             = NULL;
        struct exports_file *file           = NULL;
        size_t               len            = 0;
        int                  ret            = -EINVAL;
        unsigned long        line_number    = 0;
        char                *line           = NULL;
        struct export_dir   *expdir         = NULL;

        /* Sets whether we we should parse the entire file or just that which
         * is present in the mount state */
        gf_boolean_t    parse_complete_file = _gf_false;

        GF_VALIDATE_OR_GOTO (GF_EXP, expfile, parse_done);

        if (!ms) {
                /* If mount state is null that means that we
                 * should go through and parse the whole file
                 * since we don't have anything to compare against.
                 */
                parse_complete_file = _gf_true;
        }

        fp = fopen (filepath, "r");
        if (!fp) {
                ret = -errno;
                goto parse_done;
        }

        ret = _exp_init_parsers ();
        if (ret < 0)
                goto parse_done;

        /* Process the file line by line, with each line being parsed into
         * an struct export_dir struct. If 'parse_complete_file' is set to TRUE
         * then
         */
        while (getline (&line, &len, fp) != -1) {
                line_number++;  /* Keeping track of line number allows us to
                                 * to log which line numbers were wrong
                                 */
                strtok (line, "\n");    /* removes the newline character from
                                         * the line
                                         */

                /* Parse the line from the file into an struct export_dir
                 * structure. The process is as follows:
                 * Given a line like :
                 * "/vol @test(sec=sys,rw,anonuid=0) 10.35.11.31(sec=sys,rw)"
                 *
                 * This function will allocate an export dir and set its name
                 * to '/vol', using the function _exp_line_dir_parse ().
                 *
                 * Then it will extract the netgroups from the line, in this
                 * case it would be '@test(sec=sys,rw,anonuid=0)', and set the
                 * item structure's name to '@test'.
                 * It will also extract the options from that string and parse
                 * them into an struct export_options which will be pointed
                 * to by the item structure. This will be put into a dict
                 * which will be pointed to by the export directory structure.
                 *
                 * The same process happens above for the host string
                 * '10.35.11.32(sec=sys,rw)'
                 */
                ret = _exp_line_parse (line, &expdir, parse_complete_file, ms);
                if (ret == -ENOMEM) {
                        /* If we get memory allocation errors, we really should
                         * not continue parsing, so just free the allocated
                         * memory and exit.
                         */
                        goto free_and_done;
                }

                if (ret < 0) {
                        gf_msg (GF_EXP, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse line #%lu", line_number);
                        continue; /* Skip entering this line and continue */
                }

                if (ret == GF_EXP_PARSE_LINE_IGNORING) {
                        /* This just means the line was empty or started with a
                         * '#' or a ' ' and we are ignoring it.
                         */
                        gf_msg_debug (GF_EXP, 0,
                                      "Ignoring line #%lu because it started "
                                      "with a %c", line_number, *line);
                        continue;
                }

                if (!file) {
                        file = _exports_file_init ();
                        GF_CHECK_ALLOC_AND_LOG (GF_EXP, file, ret,
                                                "Allocation error while "
                                                "allocating file struct",
                                                parse_done);

                        file->filename = gf_strdup (filepath);
                        GF_CHECK_ALLOC_AND_LOG (GF_EXP, file, ret,
                                                "Allocation error while "
                                                "duping filepath",
                                                free_and_done);
                }

                /* If the parsing is successful store the export directory
                 * in the file structure.
                 */
                _exp_file_insert (file, expdir);
        }

        /* line got allocated through getline(), don't use GF_FREE() for it */
        free (line);

        *expfile = file;
        goto parse_done;

free_and_done:
        exp_file_deinit (file);

parse_done:
        if (fp)
                fclose (fp);
        _exp_deinit_parsers ();
        return ret;
}
