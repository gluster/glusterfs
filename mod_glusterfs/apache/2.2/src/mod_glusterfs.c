/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef CORE_PRIVATE
#define CORE_PRIVATE
#endif

#ifndef NO_CONTENT_TYPE
#define NO_CONTENT_TYPE "none"
#endif

#define BYTERANGE_FMT "%" APR_OFF_T_FMT "-%" APR_OFF_T_FMT "/%" APR_OFF_T_FMT

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_request.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_main.h>
#include <util_script.h>
#include <util_filter.h>
#include <libglusterfsclient.h>
#include <sys/uio.h>
#include <pthread.h>
#include <apr.h>
#include <apr_strings.h>
#include <apr_buckets.h>
#include <apr_fnmatch.h>
#include <apr_lib.h>

#define GLUSTERFS_INVALID_LOGLEVEL "mod_glfs: Unrecognized log-level \"%s\", "\
                                   " possible values are \"DEBUG|WARNING|"\
                                   "ERROR|CRITICAL|NONE\"\n"

#define GLUSTERFS_HANDLER "glusterfs-handler"
#define GLUSTERFS_CHUNK_SIZE 131072 

static char c_by_encoding, c_by_type, c_by_path;

#define BY_ENCODING &c_by_encoding
#define BY_TYPE &c_by_type
#define BY_PATH &c_by_path

module AP_MODULE_DECLARE_DATA glusterfs_module;
extern module core_module;

#define NO_OPTIONS          (1 <<  0)  /* Indexing options */
#define ICONS_ARE_LINKS     (1 <<  1)
#define SCAN_HTML_TITLES    (1 <<  2)
#define SUPPRESS_ICON       (1 <<  3)
#define SUPPRESS_LAST_MOD   (1 <<  4)
#define SUPPRESS_SIZE       (1 <<  5)
#define SUPPRESS_DESC       (1 <<  6)
#define SUPPRESS_PREAMBLE   (1 <<  7)
#define SUPPRESS_COLSORT    (1 <<  8)
#define SUPPRESS_RULES      (1 <<  9)
#define FOLDERS_FIRST       (1 << 10)
#define VERSION_SORT        (1 << 11)
#define TRACK_MODIFIED      (1 << 12)
#define FANCY_INDEXING      (1 << 13)
#define TABLE_INDEXING      (1 << 14)
#define IGNORE_CLIENT       (1 << 15)
#define IGNORE_CASE         (1 << 16)
#define EMIT_XHTML          (1 << 17)
#define SHOW_FORBIDDEN      (1 << 18)

#define K_NOADJUST 0
#define K_ADJUST 1
#define K_UNSET 2

/*
 * Define keys for sorting.
 */
#define K_NAME 'N'              /* Sort by file name (default) */
#define K_LAST_MOD 'M'          /* Last modification date */
#define K_SIZE 'S'              /* Size (absolute, not as displayed) */
#define K_DESC 'D'              /* Description */
#define K_VALID "NMSD"          /* String containing _all_ valid K_ opts */

#define D_ASCENDING 'A'
#define D_DESCENDING 'D'
#define D_VALID "AD"            /* String containing _all_ valid D_ opts */

/*
 * These are the dimensions of the default icons supplied with Apache.
 */
#define DEFAULT_ICON_WIDTH 20
#define DEFAULT_ICON_HEIGHT 22

/*
 * Other default dimensions.
 */
#define DEFAULT_NAME_WIDTH 23
#define DEFAULT_DESC_WIDTH 23

struct mod_glfs_ai_item {
        char *type;
        char *apply_to;
        char *apply_path;
        char *data;
};

typedef struct mod_glfs_ai_desc_t {
        char *pattern;
        char *description;
        int full_path;
        int wildcards;
} mod_glfs_ai_desc_t;

typedef enum {
        SLASH_OFF = 0,
        SLASH_ON,
        SLASH_UNSET
} mod_glfs_dir_slash_cfg;

/* static ap_filter_rec_t *mod_glfs_output_filter_handle; */

/*TODO: verify error returns to server core */

typedef struct glusterfs_dir_config {
        char                   *logfile;
        char                   *loglevel;
        char                   *specfile;
        char                   *mount_dir;
        char                   *buf;

        size_t                  xattr_file_size;
        uint32_t                cache_timeout;
        
        /* mod_dir options */
        apr_array_header_t     *index_names;
        mod_glfs_dir_slash_cfg  do_slash;

        /* autoindex options */
        char                   *default_icon;
        char                   *style_sheet;
        apr_int32_t             opts;
        apr_int32_t             incremented_opts;
        apr_int32_t             decremented_opts;
        int                     name_width;
        int                     name_adjust;
        int                     desc_width;
        int                     desc_adjust;
        int                     icon_width;
        int                     icon_height;
        char                    default_keyid;
        char                    default_direction;

        apr_array_header_t     *icon_list;
        apr_array_header_t     *alt_list;
        apr_array_header_t     *desc_list;
        apr_array_header_t     *ign_list;
        apr_array_header_t     *hdr_list;
        apr_array_header_t     *rdme_list;

        char                   *ctype;
        char                   *charset;
} glusterfs_dir_config_t;

typedef struct glusterfs_async_local {
        int                op_ret;
        int                op_errno;
        char               async_read_complete;
        off_t              length;
        off_t              read_bytes;
        glusterfs_iobuf_t *buf;
        request_rec       *request;
        pthread_mutex_t    lock;
        pthread_cond_t     cond;
}glusterfs_async_local_t;

#define GLUSTERFS_CMD_PERMS ACCESS_CONF


static glusterfs_dir_config_t *
mod_glfs_dconfig (request_rec *r)
{
        glusterfs_dir_config_t *dir_config = NULL;
        if (r->per_dir_config != NULL) {
                dir_config = ap_get_module_config (r->per_dir_config,
                                                   &glusterfs_module);
        }

        return dir_config;
}


static const char *
cmd_add_xattr_file_size (cmd_parms *cmd, void *dummy, const char *arg)
{
        glusterfs_dir_config_t *dir_config = dummy;
        dir_config->xattr_file_size = atoi (arg);
        return NULL;
}


static const char *
cmd_set_cache_timeout (cmd_parms *cmd, void *dummy, const char *arg)
{
        glusterfs_dir_config_t *dir_config = dummy;
        dir_config->cache_timeout = atoi (arg);
        return NULL;
}


static const char *
cmd_set_loglevel (cmd_parms *cmd, void *dummy, const char *arg)
{
        glusterfs_dir_config_t *dir_config = dummy;
        char                   *error = NULL;
        if (strncasecmp (arg, "DEBUG", strlen ("DEBUG")) 
            && strncasecmp (arg, "WARNING", strlen ("WARNING")) 
            && strncasecmp (arg, "CRITICAL", strlen ("CRITICAL")) 
            && strncasecmp (arg, "NONE", strlen ("NONE")) 
            && strncasecmp (arg, "ERROR", strlen ("ERROR")))
                error = GLUSTERFS_INVALID_LOGLEVEL;
        else
                dir_config->loglevel = apr_pstrdup (cmd->pool, arg);

        return error;
}

static const char *
cmd_add_logfile (cmd_parms *cmd, void *dummy, const char *arg)
{
        glusterfs_dir_config_t *dir_config = dummy;
        dir_config->logfile = apr_pstrdup (cmd->pool, arg);

        return NULL;
}


static const char *
cmd_add_volume_specfile (cmd_parms *cmd, void *dummy, const char *arg)
{
        glusterfs_dir_config_t *dir_config = dummy;

        dir_config->specfile = apr_pstrdup (cmd->pool, arg);

        return NULL;
}

#define WILDCARDS_REQUIRED 0

static const char *
cmd_add_desc (cmd_parms *cmd, void *d, const char *desc,
              const char *to)
{
        glusterfs_dir_config_t *dcfg = NULL;
        mod_glfs_ai_desc_t     *desc_entry = NULL;
        char                   *prefix = "";

        dcfg = (glusterfs_dir_config_t *) d;
        desc_entry = (mod_glfs_ai_desc_t *) apr_array_push(dcfg->desc_list);
        desc_entry->full_path = (ap_strchr_c(to, '/') == NULL) ? 0 : 1;
        desc_entry->wildcards = (WILDCARDS_REQUIRED
                                 || desc_entry->full_path
                                 || apr_fnmatch_test(to));
        if (desc_entry->wildcards) {
                prefix = desc_entry->full_path ? "*/" : "*";
                desc_entry->pattern = apr_pstrcat(dcfg->desc_list->pool,
                                                  prefix, to, "*", NULL);
        }
        else {
                desc_entry->pattern = apr_pstrdup(dcfg->desc_list->pool, to);
        }
        desc_entry->description = apr_pstrdup(dcfg->desc_list->pool, desc);
        return NULL;
}


static void push_item(apr_array_header_t *arr, char *type, const char *to,
                      const char *path, const char *data)
{
        struct mod_glfs_ai_item *p = NULL;

        p = (struct mod_glfs_ai_item *) apr_array_push(arr);

        if (!to) {
                to = "";
        }
        if (!path) {
                path = "";
        }

        p->type = type;
        p->data = data ? apr_pstrdup(arr->pool, data) : NULL;
        p->apply_path = apr_pstrcat(arr->pool, path, "*", NULL);

        if ((type == BY_PATH) && (!ap_is_matchexp(to))) {
                p->apply_to = apr_pstrcat(arr->pool, "*", to, NULL);
        }
        else if (to) {
                p->apply_to = apr_pstrdup(arr->pool, to);
        }
        else {
                p->apply_to = NULL;
        }
}


static const char *
cmd_add_ignore (cmd_parms *cmd, void *d, const char *ext)
{
        push_item(((glusterfs_dir_config_t *) d)->ign_list, 0, ext, cmd->path,
                  NULL);
        return NULL;
}


static const char *
cmd_add_header (cmd_parms *cmd, void *d, const char *name)
{
        push_item(((glusterfs_dir_config_t *) d)->hdr_list, 0, NULL, cmd->path,
                  name);
        return NULL;
}


static const char *
cmd_add_readme (cmd_parms *cmd, void *d, const char *name)
{
        push_item(((glusterfs_dir_config_t *) d)->rdme_list, 0, NULL, cmd->path,
                  name);
        return NULL;
}


static const char *
cmd_add_opts (cmd_parms *cmd, void *d, int argc, char *const argv[])
{
        int                     i = 0, option = 0;
        char                   *w = NULL;
        apr_int32_t             opts;
        apr_int32_t             opts_add;
        apr_int32_t             opts_remove;
        char                    action = 0;
        glusterfs_dir_config_t *d_cfg = (glusterfs_dir_config_t *) d;

        opts = d_cfg->opts;
        opts_add = d_cfg->incremented_opts;
        opts_remove = d_cfg->decremented_opts;

        for (i = 0; i < argc; i++) {
                w = argv[i];

                if ((*w == '+') || (*w == '-')) {
                        action = *(w++);
                }
                else {
                        action = '\0';
                }
                if (!strcasecmp(w, "FancyIndexing")) {
                        option = FANCY_INDEXING;
                }
                else if (!strcasecmp(w, "FoldersFirst")) {
                        option = FOLDERS_FIRST;
                }
                else if (!strcasecmp(w, "HTMLTable")) {
                        option = TABLE_INDEXING;
                }
                else if (!strcasecmp(w, "IconsAreLinks")) {
                        option = ICONS_ARE_LINKS;
                }
                else if (!strcasecmp(w, "IgnoreCase")) {
                        option = IGNORE_CASE;
                }
                else if (!strcasecmp(w, "IgnoreClient")) {
                        option = IGNORE_CLIENT;
                }
                else if (!strcasecmp(w, "ScanHTMLTitles")) {
                        option = SCAN_HTML_TITLES;
                }
                else if (!strcasecmp(w, "SuppressColumnSorting")) {
                        option = SUPPRESS_COLSORT;
                }
                else if (!strcasecmp(w, "SuppressDescription")) {
                        option = SUPPRESS_DESC;
                }
                else if (!strcasecmp(w, "SuppressHTMLPreamble")) {
                        option = SUPPRESS_PREAMBLE;
                }
                else if (!strcasecmp(w, "SuppressIcon")) {
                        option = SUPPRESS_ICON;
                }
                else if (!strcasecmp(w, "SuppressLastModified")) {
                        option = SUPPRESS_LAST_MOD;
                }
                else if (!strcasecmp(w, "SuppressSize")) {
                        option = SUPPRESS_SIZE;
                }
                else if (!strcasecmp(w, "SuppressRules")) {
                        option = SUPPRESS_RULES;
                }
                else if (!strcasecmp(w, "TrackModified")) {
                        option = TRACK_MODIFIED;
                }
                else if (!strcasecmp(w, "VersionSort")) {
                        option = VERSION_SORT;
                }
                else if (!strcasecmp(w, "XHTML")) {
                        option = EMIT_XHTML;
                }
                else if (!strcasecmp(w, "ShowForbidden")) {
                        option = SHOW_FORBIDDEN;
                }
                else if (!strcasecmp(w, "None")) {
                        if (action != '\0') {
                                return "Cannot combine '+' or '-' with 'None' "
                                        "keyword";
                        }
                        opts = NO_OPTIONS;
                        opts_add = 0;
                        opts_remove = 0;
                }
                else if (!strcasecmp(w, "IconWidth")) {
                        if (action != '-') {
                                d_cfg->icon_width = DEFAULT_ICON_WIDTH;
                        }
                        else {
                                d_cfg->icon_width = 0;
                        }
                }
                else if (!strncasecmp(w, "IconWidth=", 10)) {
                        if (action == '-') {
                                return "Cannot combine '-' with IconWidth=n";
                        }
                        d_cfg->icon_width = atoi(&w[10]);
                }
                else if (!strcasecmp(w, "IconHeight")) {
                        if (action != '-') {
                                d_cfg->icon_height = DEFAULT_ICON_HEIGHT;
                        }
                        else {
                                d_cfg->icon_height = 0;
                        }
                }
                else if (!strncasecmp(w, "IconHeight=", 11)) {
                        if (action == '-') {
                                return "Cannot combine '-' with IconHeight=n";
                        }
                        d_cfg->icon_height = atoi(&w[11]);
                }
                else if (!strcasecmp(w, "NameWidth")) {
                        if (action != '-') {
                                return "NameWidth with no value may only appear"
                                        " as "
                                        "'-NameWidth'";
                        }
                        d_cfg->name_width = DEFAULT_NAME_WIDTH;
                        d_cfg->name_adjust = K_NOADJUST;
                }
                else if (!strncasecmp(w, "NameWidth=", 10)) {
                        if (action == '-') {
                                return "Cannot combine '-' with NameWidth=n";
                        }
                        if (w[10] == '*') {
                                d_cfg->name_adjust = K_ADJUST;
                        }
                        else {
                                int width = atoi(&w[10]);

                                if (width && (width < 5)) {
                                        return "NameWidth value must be greater"
                                                " than 5";
                                }
                                d_cfg->name_width = width;
                                d_cfg->name_adjust = K_NOADJUST;
                        }
                }
                else if (!strcasecmp(w, "DescriptionWidth")) {
                        if (action != '-') {
                                return "DescriptionWidth with no value may only"
                                        " appear as "
                                        "'-DescriptionWidth'";
                        }
                        d_cfg->desc_width = DEFAULT_DESC_WIDTH;
                        d_cfg->desc_adjust = K_NOADJUST;
                }
                else if (!strncasecmp(w, "DescriptionWidth=", 17)) {
                        if (action == '-') {
                                return "Cannot combine '-' with "
                                        "DescriptionWidth=n";
                        }
                        if (w[17] == '*') {
                                d_cfg->desc_adjust = K_ADJUST;
                        }
                        else {
                                int width = atoi(&w[17]);

                                if (width && (width < 12)) {
                                        return "DescriptionWidth value must be "
                                                "greater than 12";
                                }
                                d_cfg->desc_width = width;
                                d_cfg->desc_adjust = K_NOADJUST;
                        }
                }
                else if (!strncasecmp(w, "Type=", 5)) {
                        d_cfg->ctype = apr_pstrdup(cmd->pool, &w[5]);
                }
                else if (!strncasecmp(w, "Charset=", 8)) {
                        d_cfg->charset = apr_pstrdup(cmd->pool, &w[8]);
                }
                else {
                        return "Invalid directory indexing option";
                }
                if (action == '\0') {
                        opts |= option;
                        opts_add = 0;
                        opts_remove = 0;
                }
                else if (action == '+') {
                        opts_add |= option;
                        opts_remove &= ~option;
                }
                else {
                        opts_remove |= option;
                        opts_add &= ~option;
                }
        }
        if ((opts & NO_OPTIONS) && (opts & ~NO_OPTIONS)) {
                return "Cannot combine other IndexOptions keywords with 'None'";
        }
        d_cfg->incremented_opts = opts_add;
        d_cfg->decremented_opts = opts_remove;
        d_cfg->opts = opts;
        return NULL;
}


static const char *
cmd_set_default_order(cmd_parms *cmd, void *m,
                      const char *direction, const char *key)
{
        glusterfs_dir_config_t *d_cfg = (glusterfs_dir_config_t *) m;

        if (!strcasecmp(direction, "Ascending")) {
                d_cfg->default_direction = D_ASCENDING;
        }
        else if (!strcasecmp(direction, "Descending")) {
                d_cfg->default_direction = D_DESCENDING;
        }
        else {
                return "First keyword must be 'Ascending' or 'Descending'";
        }

        if (!strcasecmp(key, "Name")) {
                d_cfg->default_keyid = K_NAME;
        }
        else if (!strcasecmp(key, "Date")) {
                d_cfg->default_keyid = K_LAST_MOD;
        }
        else if (!strcasecmp(key, "Size")) {
                d_cfg->default_keyid = K_SIZE;
        }
        else if (!strcasecmp(key, "Description")) {
                d_cfg->default_keyid = K_DESC;
        }
        else {
                return "Second keyword must be 'Name', 'Date', 'Size', or "
                        "'Description'";
        }

        return NULL;
}


static char c_by_encoding, c_by_type, c_by_path;

#define BY_ENCODING &c_by_encoding
#define BY_TYPE &c_by_type
#define BY_PATH &c_by_path

/*
 * This routine puts the standard HTML header at the top of the index page.
 * We include the DOCTYPE because we may be using features therefrom (i.e.,
 * HEIGHT and WIDTH attributes on the icons if we're FancyIndexing).
 */
static void emit_preamble(request_rec *r, int xhtml, const char *title)
{
        glusterfs_dir_config_t *d;

        d = (glusterfs_dir_config_t *) ap_get_module_config(r->per_dir_config,
                                                            &glusterfs_module);

        if (xhtml) {
                ap_rvputs(r, DOCTYPE_XHTML_1_0T,
                          "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                          " <head>\n  <title>Index of ", title,
                          "</title>\n", NULL);
        } else {
                ap_rvputs(r, DOCTYPE_HTML_3_2,
                          "<html>\n <head>\n"
                          "  <title>Index of ", title,
                          "</title>\n", NULL);
        }

        if (d->style_sheet != NULL) {
                ap_rvputs(r, "  <link rel=\"stylesheet\" href=\"",
                          d->style_sheet, "\" type=\"text/css\"",
                          xhtml ? " />\n" : ">\n", NULL);
        }
        ap_rvputs(r, " </head>\n <body>\n", NULL);
}


static const char *cmd_add_alt(cmd_parms *cmd, void *d, const char *alt,
                               const char *to)
{
        if (cmd->info == BY_PATH) {
                if (!strcmp(to, "**DIRECTORY**")) {
                        to = "^^DIRECTORY^^";
                }
        }
        if (cmd->info == BY_ENCODING) {
                char *tmp = apr_pstrdup(cmd->pool, to);
                ap_str_tolower(tmp);
                to = tmp;
        }

        push_item(((glusterfs_dir_config_t *) d)->alt_list, cmd->info, to,
                  cmd->path, alt);
        return NULL;
}

static const char *cmd_add_icon(cmd_parms *cmd, void *d, const char *icon,
                                const char *to)
{
        char *iconbak = apr_pstrdup(cmd->pool, icon);
        char *alt = NULL, *cl = NULL, *tmp = NULL;

        if (icon[0] == '(') {
                cl = strchr(iconbak, ')');

                if (cl == NULL) {
                        return "missing closing paren";
                }
                alt = ap_getword_nc(cmd->pool, &iconbak, ',');
                *cl = '\0';                             /* Lose closing paren */
                cmd_add_alt(cmd, d, &alt[1], to);
        }
        if (cmd->info == BY_PATH) {
                if (!strcmp(to, "**DIRECTORY**")) {
                        to = "^^DIRECTORY^^";
                }
        }
        if (cmd->info == BY_ENCODING) {
                tmp = apr_pstrdup(cmd->pool, to);
                ap_str_tolower(tmp);
                to = tmp;
        }

        push_item(((glusterfs_dir_config_t *) d)->icon_list, cmd->info, to,
                  cmd->path, iconbak);
        return NULL;
}


static void *
mod_glfs_create_dir_config(apr_pool_t *p, char *dirspec)
{
        glusterfs_dir_config_t *dir_config = NULL;

        dir_config = (glusterfs_dir_config_t *) apr_pcalloc(p,
                                                            sizeof(*dir_config));

        dir_config->mount_dir = dirspec;
        dir_config->logfile = dir_config->specfile = (char *)0;
        dir_config->loglevel = "warning";
        dir_config->cache_timeout = 0;
        dir_config->buf = NULL;

        /* mod_dir options init */
        dir_config->index_names = NULL;
        dir_config->do_slash = SLASH_UNSET;

        /* autoindex options init */
        dir_config->icon_width = 0;
        dir_config->icon_height = 0;
        dir_config->name_width = DEFAULT_NAME_WIDTH;
        dir_config->name_adjust = K_UNSET;
        dir_config->desc_width = DEFAULT_DESC_WIDTH;
        dir_config->desc_adjust = K_UNSET;
        dir_config->icon_list = apr_array_make(p, 4,
                                               sizeof(struct mod_glfs_ai_item));
        dir_config->alt_list = apr_array_make(p, 4,
                                              sizeof(struct mod_glfs_ai_item));
        dir_config->desc_list = apr_array_make(p, 4,
                                               sizeof(mod_glfs_ai_desc_t));
        dir_config->ign_list = apr_array_make(p, 4,
                                              sizeof(struct mod_glfs_ai_item));
        dir_config->hdr_list = apr_array_make(p, 4,
                                              sizeof(struct mod_glfs_ai_item));
        dir_config->rdme_list = apr_array_make(p, 4,
                                               sizeof(struct mod_glfs_ai_item));
        dir_config->opts = 0;
        dir_config->incremented_opts = 0;
        dir_config->decremented_opts = 0;
        dir_config->default_keyid = '\0';
        dir_config->default_direction = '\0';

        return (void *) dir_config;
}


static void *
mod_glfs_merge_dir_config(apr_pool_t *p, void *parent_conf,
                          void *newloc_conf)
{
        glusterfs_dir_config_t *new = NULL;
        glusterfs_dir_config_t *add = NULL;
        glusterfs_dir_config_t *base = NULL;

        new = (glusterfs_dir_config_t *) 
                apr_pcalloc(p, sizeof(glusterfs_dir_config_t));
        add = newloc_conf;
        base = parent_conf;

        if (add->logfile)
                new->logfile = apr_pstrdup (p, add->logfile);

        if (add->loglevel)
                new->loglevel = apr_pstrdup (p, add->loglevel);

        if (add->specfile)
                new->specfile = apr_pstrdup (p, add->specfile);

        if (add->mount_dir)
                new->mount_dir = apr_pstrdup (p, add->mount_dir);

        new->xattr_file_size = add->xattr_file_size;
        new->cache_timeout = add->cache_timeout;
        new->buf = add->buf;

        /* mod_dir */
        new->index_names = add->index_names ? 
                add->index_names : base->index_names;
        new->do_slash =
                (add->do_slash == SLASH_UNSET) ? base->do_slash : add->do_slash;

        /* auto index */
        new->default_icon = add->default_icon ? add->default_icon
                : base->default_icon;
        new->style_sheet = add->style_sheet ? add->style_sheet
                : base->style_sheet;
        new->icon_height = add->icon_height ? 
                add->icon_height : base->icon_height;
        new->icon_width = add->icon_width ? add->icon_width : base->icon_width;

        new->ctype = add->ctype ? add->ctype : base->ctype;
        new->charset = add->charset ? add->charset : base->charset;

        new->alt_list = apr_array_append(p, add->alt_list, base->alt_list);
        new->ign_list = apr_array_append(p, add->ign_list, base->ign_list);
        new->hdr_list = apr_array_append(p, add->hdr_list, base->hdr_list);
        new->desc_list = apr_array_append(p, add->desc_list, base->desc_list);
        new->icon_list = apr_array_append(p, add->icon_list, base->icon_list);
        new->rdme_list = apr_array_append(p, add->rdme_list, base->rdme_list);
        if (add->opts & NO_OPTIONS) {
                /*
                 * If the current directory says 'no options' then we also
                 * clear any incremental mods from being inheritable further down.
                 */
                new->opts = NO_OPTIONS;
                new->incremented_opts = 0;
                new->decremented_opts = 0;
        }
        else {
                /*
                 * If there were any nonincremental options selected for
                 * this directory, they dominate and we don't inherit *anything.*
                 * Contrariwise, we *do* inherit if the only settings here are
                 * incremental ones.
                 */
                if (add->opts == 0) {
                        new->incremented_opts = (base->incremented_opts
                                                 | add->incremented_opts)
                                & ~add->decremented_opts;
                        new->decremented_opts = (base->decremented_opts
                                                 | add->decremented_opts);
                        /*
                         * We may have incremental settings, so make sure we 
                         * don't inadvertently inherit an IndexOptions None 
                         * from above.
                         */
                        new->opts = (base->opts & ~NO_OPTIONS);
                }
                else {
                        /*
                         * There are local nonincremental settings, which clear
                         * all inheritance from above.  They *are* the new 
                         * base settings.
                         */
                        new->opts = add->opts;;
                }
                /*
                 * We're guaranteed that there'll be no overlap between
                 * the add-options and the remove-options.
                 */
                new->opts |= new->incremented_opts;
                new->opts &= ~new->decremented_opts;
        }
        /*
         * Inherit the NameWidth settings if there aren't any specific to
         * the new location; otherwise we'll end up using the defaults set 
         * in the config-rec creation routine.
         */
        if (add->name_adjust == K_UNSET) {
                new->name_width = base->name_width;
                new->name_adjust = base->name_adjust;
        }
        else {
                new->name_width = add->name_width;
                new->name_adjust = add->name_adjust;
        }

        /*
         * Likewise for DescriptionWidth.
         */
        if (add->desc_adjust == K_UNSET) {
                new->desc_width = base->desc_width;
                new->desc_adjust = base->desc_adjust;
        }
        else {
                new->desc_width = add->desc_width;
                new->desc_adjust = add->desc_adjust;
        }

        new->default_keyid = add->default_keyid ? add->default_keyid
                : base->default_keyid;
        new->default_direction = add->default_direction ? add->default_direction
                : base->default_direction;

        return (void *) new;
}


static void 
mod_glfs_child_init(apr_pool_t *p, server_rec *s)
{
        int                      i = 0, num_sec = 0, ret = 0;
        core_server_config      *sconf = NULL;
        ap_conf_vector_t       **sec_ent = NULL;
        glusterfs_dir_config_t  *dir_config = NULL;
        glusterfs_init_params_t  ctx = {0, };
  
        sconf = (core_server_config *) ap_get_module_config (s->module_config,
                                                             &core_module);
        sec_ent = (ap_conf_vector_t **) sconf->sec_url->elts;
        num_sec = sconf->sec_url->nelts;

        for (i = 0; i < num_sec; i++) {
                dir_config = ap_get_module_config (sec_ent[i],
                                                   &glusterfs_module);

                if (dir_config) {
                        memset (&ctx, 0, sizeof (ctx));

                        ctx.logfile = dir_config->logfile;
                        ctx.loglevel = dir_config->loglevel;
                        ctx.lookup_timeout = dir_config->cache_timeout;
                        ctx.stat_timeout = dir_config->cache_timeout;
                        ctx.specfile = dir_config->specfile;

                        ret = glusterfs_mount (dir_config->mount_dir, &ctx);
                        if (ret != 0) {
                                ap_log_error(APLOG_MARK, APLOG_ERR,
                                             APR_EGENERAL, s, 
                                             "mod_glfs_child_init: "
                                             "glusterfs_init failed, check "
                                             "glusterfs logfile %s for more "
                                             "details", 
                                             dir_config->logfile);
                        }
                }
                dir_config = NULL;
        }
}


static void 
mod_glfs_child_exit(server_rec *s, apr_pool_t *p)
{
        int                      i = 0, num_sec = 0;
        core_server_config      *sconf = NULL;
        ap_conf_vector_t       **sec_ent = NULL;
        glusterfs_dir_config_t  *dir_config = NULL;
        glusterfs_init_params_t  ctx = {0, };

        sconf = ap_get_module_config(s->module_config,
                                     &core_module);
        sec_ent = (ap_conf_vector_t **) sconf->sec_url->elts;
        num_sec = sconf->sec_url->nelts;
        for (i = 0; i < num_sec; i++) {
                dir_config = ap_get_module_config (sec_ent[i],
                                                   &glusterfs_module);
                if (dir_config) {
                        glusterfs_umount (dir_config->mount_dir);
                }
        }
}

static apr_filetype_e filetype_from_mode(mode_t mode)
{
        apr_filetype_e type = APR_NOFILE;

        if (S_ISREG(mode))
                type = APR_REG;
        else if (S_ISDIR(mode))
                type = APR_DIR;
        else if (S_ISCHR(mode))
                type = APR_CHR;
        else if (S_ISBLK(mode))
                type = APR_BLK;
        else if (S_ISFIFO(mode))
                type = APR_PIPE;
        else if (S_ISLNK(mode))
                type = APR_LNK;
        else if (S_ISSOCK(mode))
                type = APR_SOCK;
        else
                type = APR_UNKFILE;
        return type;
}


static void fill_out_finfo(apr_finfo_t *finfo, struct stat *info,
                           apr_int32_t wanted)
{ 
        finfo->valid = APR_FINFO_MIN | APR_FINFO_IDENT | APR_FINFO_NLINK
                | APR_FINFO_OWNER | APR_FINFO_PROT;
        finfo->protection = apr_unix_mode2perms(info->st_mode);
        finfo->filetype = filetype_from_mode(info->st_mode);
        finfo->user = info->st_uid;
        finfo->group = info->st_gid;
        finfo->size = info->st_size;
        finfo->device = info->st_dev;
        finfo->nlink = info->st_nlink;

        /* Check for overflow if storing a 64-bit st_ino in a 32-bit
         * apr_ino_t for LFS builds: */
        if (sizeof(apr_ino_t) >= sizeof(info->st_ino)
            || (apr_ino_t)info->st_ino == info->st_ino) {
                finfo->inode = info->st_ino;
        } else {
                finfo->valid &= ~APR_FINFO_INODE;
        }

        apr_time_ansi_put(&finfo->atime, info->st_atime);
#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
        finfo->atime += info->st_atim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_ATIMENSEC)
        finfo->atime += info->st_atimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_ATIME_N)
        finfo->ctime += info->st_atime_n / APR_TIME_C(1000);
#endif

        apr_time_ansi_put(&finfo->mtime, info->st_mtime);
#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
        finfo->mtime += info->st_mtim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
        finfo->mtime += info->st_mtimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
        finfo->ctime += info->st_mtime_n / APR_TIME_C(1000);
#endif

        apr_time_ansi_put(&finfo->ctime, info->st_ctime);
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
        finfo->ctime += info->st_ctim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
        finfo->ctime += info->st_ctimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_CTIME_N)
        finfo->ctime += info->st_ctime_n / APR_TIME_C(1000);
#endif

#ifdef HAVE_STRUCT_STAT_ST_BLOCKS
#ifdef DEV_BSIZE
        finfo->csize = (apr_off_t)info->st_blocks * (apr_off_t)DEV_BSIZE;
#else
        finfo->csize = (apr_off_t)info->st_blocks * (apr_off_t)512;
#endif
        finfo->valid |= APR_FINFO_CSIZE;
#endif
}


static int 
mod_glfs_map_to_storage(request_rec *r)
{
        glusterfs_dir_config_t *dir_config = NULL, *tmp = NULL;
        int                     access_status = 0, ret = 0;
        char                   *path = NULL;
        struct stat             st = {0, };
        core_server_config     *sconf = NULL;
        ap_conf_vector_t      **sec_ent = NULL;
        int                     num_sec = 0, i = 0;

        sconf = (core_server_config *) ap_get_module_config (r->server->module_config,
                                                             &core_module);
        sec_ent = (ap_conf_vector_t **) sconf->sec_url->elts;
        num_sec = sconf->sec_url->nelts;

        for (i = 0; i < num_sec; i++) {
                tmp = ap_get_module_config (sec_ent[i], &glusterfs_module);

                if (tmp && !strncmp (tmp->mount_dir, r->uri,
                                     strlen (tmp->mount_dir))) {
                        if (!dir_config || 
                            strlen (tmp->mount_dir) 
                            > strlen (dir_config->mount_dir)) {
                                dir_config = tmp;
                        }
                }

        }

        if (dir_config && dir_config->mount_dir 
            && !(strncmp (apr_pstrcat (r->pool, dir_config->mount_dir, "/",
                                       NULL), r->uri,
                          strlen (dir_config->mount_dir) + 1)
                 && !r->handler)) 
                r->handler = GLUSTERFS_HANDLER;

        if (!r->handler || (r->handler && strcmp (r->handler,
                                                  GLUSTERFS_HANDLER)))
                return DECLINED;

        path = r->uri;

        memset (&r->finfo, 0, sizeof (r->finfo));

        dir_config->buf = calloc (1, dir_config->xattr_file_size);
        if (!dir_config->buf) {
                return HTTP_INTERNAL_SERVER_ERROR;
        }

        ret = glusterfs_get (path, dir_config->buf, 
                             dir_config->xattr_file_size, &st);

        if (ret == -1 || st.st_size > dir_config->xattr_file_size 
            || S_ISDIR (st.st_mode)) {
                free (dir_config->buf);
                dir_config->buf = NULL;

                if (ret == -1) {
                        int error = HTTP_NOT_FOUND;
                        char *emsg = NULL;
                        if (r->path_info == NULL) {
                                emsg = apr_pstrcat(r->pool, strerror (errno),
                                                   r->filename, NULL);
                        }
                        else {
                                emsg = apr_pstrcat(r->pool, strerror (errno),
                                                   r->filename, r->path_info,
                                                   NULL);
                        }
                        ap_log_rerror(APLOG_MARK, APLOG_ERR|APLOG_NOERRNO, 0,
                                      r, "%s", emsg);
                        if (errno != ENOENT) {
                                error = HTTP_INTERNAL_SERVER_ERROR;
                        }
                        return error;
                }
        }

        r->finfo.pool = r->pool;
        r->finfo.fname = r->filename;
        fill_out_finfo (&r->finfo, &st, 
                        APR_FINFO_MIN | APR_FINFO_IDENT | APR_FINFO_NLINK |
                        APR_FINFO_OWNER | APR_FINFO_PROT);
                        
        /* allow core module to run directory_walk() and location_walk() */
        return DECLINED;
}


static int 
mod_glfs_readv_async_cbk (int32_t op_ret, int32_t op_errno,
                          glusterfs_iobuf_t *buf, void *cbk_data)
{
        glusterfs_async_local_t *local = cbk_data;

        pthread_mutex_lock (&local->lock);
        {
                local->async_read_complete = 1;
                local->buf = buf;
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                pthread_cond_signal (&local->cond);
        }
        pthread_mutex_unlock (&local->lock);

        return 0;
}

/* use read_async just to avoid memcpy of read buffer in libglusterfsclient */
static int
mod_glfs_read_async (request_rec *r, apr_bucket_brigade *bb,
                     glusterfs_file_t fd,
                     apr_off_t offset, apr_off_t length)
{
        glusterfs_async_local_t local = {0, };
        off_t                   end = 0;
        int                     nbytes = 0, complete = 0;
        conn_rec               *c = r->connection;
        apr_bucket             *e = NULL;
        apr_status_t            status = APR_SUCCESS;
        glusterfs_iobuf_t      *buf = NULL;
                
        if (length == 0) {
                return 0;
        }

        pthread_cond_init (&local.cond, NULL);
        pthread_mutex_init (&local.lock, NULL);
  
        memset (&local, 0, sizeof (local));
        local.request = r;

        if (length > 0)
                end = offset + length;

        do {
                if (length > 0) {
                        nbytes = end - offset;
                        if (nbytes > GLUSTERFS_CHUNK_SIZE)
                                nbytes = GLUSTERFS_CHUNK_SIZE;
                } else
                        nbytes = GLUSTERFS_CHUNK_SIZE;

                glusterfs_read_async(fd, 
                                     nbytes,
                                     offset,
                                     mod_glfs_readv_async_cbk,
                                     (void *)&local);

                pthread_mutex_lock (&local.lock);
                {
                        while (!local.async_read_complete) {
                                pthread_cond_wait (&local.cond, &local.lock);
                        }

                        local.async_read_complete = 0;
                        buf = local.buf;

                        if (length < 0)
                                complete = (local.op_ret <= 0);
                        else {
                                local.read_bytes += local.op_ret;
                                complete = ((local.read_bytes == length) ||
                                            (local.op_ret < 0));
                        }
                }
                pthread_mutex_unlock (&local.lock);

                if (!bb) {
                        bb = apr_brigade_create (r->pool, c->bucket_alloc); 
                }
                apr_brigade_writev (bb, NULL, NULL, buf->vector, buf->count);

                /* 
                 * make sure all the data is written out, since we call 
                 * glusterfs_free on buf once ap_pass_brigade returns
                 */
                e = apr_bucket_flush_create (c->bucket_alloc);
                APR_BRIGADE_INSERT_TAIL (bb, e);

                status = ap_pass_brigade (r->output_filters, bb);
                if (status != APR_SUCCESS) {
                        /* no way to know what type of error occurred */
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, status, r,
                                      "mod_glfs_handler: ap_pass_brigade "
                                      "returned %i",
                                      status);
                        complete = 1;
                        local.op_ret = -1;
                }

                glusterfs_free (buf);

                /* 
                 * bb has already been cleaned up by core_output_filter, 
                 * just being paranoid
                 */
                apr_brigade_cleanup (bb);

                offset += nbytes;
        } while (!complete);

        return (local.op_ret < 0 ? HTTP_INTERNAL_SERVER_ERROR : OK);
}

static int 
parse_byterange(char *range, apr_off_t clength,
                apr_off_t *start, apr_off_t *end)
{
        char       *dash = NULL, *errp = NULL;
        apr_off_t   number;

        dash = strchr(range, '-');
        if (!dash) {
                return 0;
        }

        if ((dash == range)) {
                /* In the form "-5" */
                if (apr_strtoff(&number, dash+1, &errp, 10) || *errp) {
                        return 0;
                }
                *start = clength - number;
                *end = clength - 1; 
        }
        else {
                *dash++ = '\0';
                if (apr_strtoff(&number, range, &errp, 10) || *errp) {
                        return 0;
                }
                *start = number;
                if (*dash) {
                        if (apr_strtoff(&number, dash, &errp, 10) || *errp) {
                                return 0;
                        }
                        *end = number;
                }
                else {                  /* "5-" */
                        *end = clength - 1;
                }
        }

        if (*start < 0) {
                *start = 0;
        }

        if (*end >= clength) {
                *end = clength - 1;
        }

        if (*start > *end) {
                return -1;
        }

        return (*start > 0 || *end < clength);
}


static int use_range_x(request_rec *r)
{
        const char *ua = NULL;
        return (apr_table_get(r->headers_in, "Request-Range")
                || ((ua = apr_table_get(r->headers_in, "User-Agent"))
                    && ap_strstr_c(ua, "MSIE 3")));
}


static int ap_set_byterange(request_rec *r)
{
        const char *range = NULL, *if_range = NULL, *match = NULL, *ct = NULL;
        int         num_ranges = 0;

        if (r->assbackwards) {
                return 0;
        }

        /* Check for Range request-header (HTTP/1.1) or Request-Range for
         * backwards-compatibility with second-draft Luotonen/Franks
         * byte-ranges (e.g. Netscape Navigator 2-3).
         *
         * We support this form, with Request-Range, and (farther down) we
         * send multipart/x-byteranges instead of multipart/byteranges for
         * Request-Range based requests to work around a bug in Netscape
         * Navigator 2-3 and MSIE 3.
         */

        if (!(range = apr_table_get(r->headers_in, "Range"))) {
                range = apr_table_get(r->headers_in, "Request-Range");
        }

        if (!range || strncasecmp(range, "bytes=", 6) || r->status != HTTP_OK) {
                return 0;
        }

        /* is content already a single range? */
        if (apr_table_get(r->headers_out, "Content-Range")) {
                return 0;
        }

        /* is content already a multiple range? */
        if ((ct = apr_table_get(r->headers_out, "Content-Type"))
            && (!strncasecmp(ct, "multipart/byteranges", 20)
                || !strncasecmp(ct, "multipart/x-byteranges", 22))) {
                return 0;
        }

        /* Check the If-Range header for Etag or Date.
         * Note that this check will return false (as required) if either
         * of the two etags are weak.
         */
        if ((if_range = apr_table_get(r->headers_in, "If-Range"))) {
                if (if_range[0] == '"') {
                        if (!(match = apr_table_get(r->headers_out, "Etag"))
                            || (strcmp(if_range, match) != 0)) {
                                return 0;
                        }
                }
                else if (!(match = apr_table_get(r->headers_out,
                                                 "Last-Modified"))
                         || (strcmp(if_range, match) != 0)) {
                        return 0;
                }
        }

        if (!ap_strchr_c(range, ',')) {
                /* a single range */
                num_ranges = 1;
        }
        else {
                /* a multiple range */
                num_ranges = 2;
        }

        r->status = HTTP_PARTIAL_CONTENT;
        r->range = range + 6;

        return num_ranges;
}


static void
mod_glfs_handle_byte_ranges (request_rec *r, glusterfs_file_t fd,
                             int num_ranges)
{
        conn_rec           *c = r->connection;
        char               *ts = NULL, *boundary = NULL, *bound_head = NULL;
        const char         *orig_ct = NULL;
        char               *current = NULL, *end = NULL;
        apr_bucket_brigade *bsend = NULL;
        apr_bucket         *e = NULL;
        apr_off_t           range_start, range_end;
        apr_status_t        rv = APR_SUCCESS;
        char                found = 0;
        apr_bucket         *e2 = NULL, *ec = NULL;

        orig_ct = ap_make_content_type (r, r->content_type);

        if (num_ranges > 1) {
                boundary = apr_psprintf(r->pool, "%" APR_UINT64_T_HEX_FMT "%lx",
                                        (apr_uint64_t)r->request_time,
                                        (long) getpid());

                ap_set_content_type(r, apr_pstrcat(r->pool, "multipart",
                                                   use_range_x(r) ? "/x-" : "/",
                                                   "byteranges; boundary=",
                                                   boundary, NULL));

                if (strcasecmp(orig_ct, NO_CONTENT_TYPE)) {
                        bound_head = apr_pstrcat(r->pool,
                                                 CRLF "--", boundary,
                                                 CRLF "Content-type: ",
                                                 orig_ct,
                                                 CRLF "Content-range: bytes ",
                                                 NULL);
                }
                else {
                        /* if we have no type for the content, do our best */
                        bound_head = apr_pstrcat(r->pool,
                                                 CRLF "--", boundary,
                                                 CRLF "Content-range: bytes ",
                                                 NULL);
                }
        }

        while ((current = ap_getword(r->pool, &r->range, ','))
               && (rv = parse_byterange(current, r->finfo.size, &range_start,
                                        &range_end))) {
                bsend = NULL;
                if (rv == -1) {
                        continue;
                }

                found = 1;

                /* For single range requests, we must produce Content-Range 
                 * header. Otherwise, we need to produce the multipart 
                 * boundaries.
                 */
                if (num_ranges == 1) {
                        apr_table_setn(r->headers_out, "Content-Range",
                                       apr_psprintf(r->pool,
                                                    "bytes " BYTERANGE_FMT,
                                                    range_start, range_end,
                                                    r->finfo.size));
                }
                else {
                        /* this brigade holds what we will be sending */
                        bsend = apr_brigade_create(r->pool, c->bucket_alloc);

                        e = apr_bucket_pool_create(bound_head,
                                                   strlen(bound_head),
                                                   r->pool, c->bucket_alloc);
                        APR_BRIGADE_INSERT_TAIL(bsend, e);

                        ts = apr_psprintf(r->pool, BYTERANGE_FMT CRLF CRLF,
                                          range_start, range_end,
                                          r->finfo.size);
                        e = apr_bucket_pool_create(ts, strlen(ts), r->pool,
                                                   c->bucket_alloc);
                        APR_BRIGADE_INSERT_TAIL(bsend, e);
                }
                mod_glfs_read_async (r, bsend, fd, range_start,
                                     (range_end + 1 - range_start));
        }

        bsend = apr_brigade_create (r->pool, c->bucket_alloc);

        if (found == 0) {
                r->status = HTTP_OK;
                /* bsend is assumed to be empty if we get here. */
                e = ap_bucket_error_create(HTTP_RANGE_NOT_SATISFIABLE, NULL,
                                           r->pool, c->bucket_alloc);
                APR_BRIGADE_INSERT_TAIL(bsend, e);
                e = apr_bucket_eos_create(c->bucket_alloc);
                APR_BRIGADE_INSERT_TAIL(bsend, e);
                ap_pass_brigade (r->output_filters, bsend);
                return;
        }

        if (num_ranges > 1) {
                /* add the final boundary */
                end = apr_pstrcat(r->pool, CRLF "--", boundary, "--" CRLF,
                                  NULL);
//                ap_xlate_proto_to_ascii(end, strlen(end));
                e = apr_bucket_pool_create(end, strlen(end), r->pool,
                                           c->bucket_alloc);
                APR_BRIGADE_INSERT_TAIL(bsend, e);
        }

        ap_pass_brigade (r->output_filters, bsend);
}



/****************************************************************
 *
 * Looking things up in config entries...
 */

/* Structure used to hold entries when we're actually building an index */

struct ent {
        char       *name;
        char       *icon;
        char       *alt;
        char       *desc;
        apr_off_t   size;
        apr_time_t  lm;
        struct ent *next;
        int         ascending, ignore_case, version_sort;
        char        key;
        int         isdir;
};

static char *find_item(request_rec *r, apr_array_header_t *list, int path_only)
{
        const char              *content_type = NULL;
        const char              *content_encoding = NULL;
        char                    *path = NULL;
        int                      i = 0;
        struct mod_glfs_ai_item *items = NULL;
        struct mod_glfs_ai_item *p = NULL;

        content_type = ap_field_noparam(r->pool, r->content_type);
        content_encoding = r->content_encoding;
        path = r->filename;
        items = (struct mod_glfs_ai_item *) list->elts;

        for (i = 0; i < list->nelts; ++i) {
                p = &items[i];
                /* Special cased for ^^DIRECTORY^^ and ^^BLANKICON^^ */
                if ((path[0] == '^') || (!ap_strcmp_match(path,
                                                          p->apply_path))) {
                        if (!*(p->apply_to)) {
                                return p->data;
                        }
                        else if (p->type == BY_PATH || path[0] == '^') {
                                if (!ap_strcmp_match(path, p->apply_to)) {
                                        return p->data;
                                }
                        }
                        else if (!path_only) {
                                if (!content_encoding) {
                                        if (p->type == BY_TYPE) {
                                                if (content_type
                                                    && !ap_strcasecmp_match(content_type,
                                                                            p->apply_to)) {
                                                        return p->data;
                                                }
                                        }
                                }
                                else {
                                        if (p->type == BY_ENCODING) {
                                                if (!ap_strcasecmp_match(content_encoding,
                                                                         p->apply_to)) {
                                                        return p->data;
                                                }
                                        }
                                }
                        }
                }
        }
        return NULL;
}

#define find_icon(d,p,t) find_item(p,d->icon_list,t)
#define find_alt(d,p,t) find_item(p,d->alt_list,t)
#define find_header(d,p) find_item(p,d->hdr_list,0)
#define find_readme(d,p) find_item(p,d->rdme_list,0)

static char *find_default_item(char *bogus_name, apr_array_header_t *list)
{
        request_rec r;
        /* Bleah.  I tried to clean up find_item, and it lead to this bit
         * of ugliness.   Note that the fields initialized are precisely
         * those that find_item looks at...
         */
        r.filename = bogus_name;
        r.content_type = r.content_encoding = NULL;
        return find_item(&r, list, 1);
}

#define find_default_icon(d,n) find_default_item(n, d->icon_list)
#define find_default_alt(d,n) find_default_item(n, d->alt_list)

/*
 * Look through the list of pattern/description pairs and return the first one
 * if any) that matches the filename in the request.  If multiple patterns
 * match, only the first one is used; since the order in the array is the
 * same as the order in which directives were processed, earlier matching
 * directives will dominate.
 */

#ifdef CASE_BLIND_FILESYSTEM
#define MATCH_FLAGS APR_FNM_CASE_BLIND
#else
#define MATCH_FLAGS 0
#endif

static char *find_desc(glusterfs_dir_config_t *dcfg, const char *filename_full)
{
        int                 i = 0;
        mod_glfs_ai_desc_t *list = NULL;
        const char         *filename_only = NULL, *filename = NULL;
        mod_glfs_ai_desc_t *tuple = &list[i];
        int                 found = 0;

        list = (mod_glfs_ai_desc_t *) dcfg->desc_list->elts;
        /*
         * If the filename includes a path, extract just the name itself
         * for the simple matches.
         */
        if ((filename_only = ap_strrchr_c(filename_full, '/')) == NULL) {
                filename_only = filename_full;
        }
        else {
                filename_only++;
        }
        for (i = 0; i < dcfg->desc_list->nelts; ++i) {
                /*
                 * Only use the full-path filename if the pattern contains '/'s.
                 */
                filename = (tuple->full_path) ? filename_full : filename_only;
                /*
                 * Make the comparison using the cheapest method; only do
                 * wildcard checking if we must.
                 */
                if (tuple->wildcards) {
                        found = (apr_fnmatch(tuple->pattern, filename,
                                             MATCH_FLAGS) == 0);
                }
                else {
                        found = (ap_strstr_c(filename, tuple->pattern) != NULL);
                }
                if (found) {
                        return tuple->description;
                }
        }
        return NULL;
}

static int ignore_entry(glusterfs_dir_config_t *d, char *path)
{
        apr_array_header_t      *list = d->ign_list;
        struct mod_glfs_ai_item *items = (struct mod_glfs_ai_item *) list->elts;
        char                    *tt = NULL, *ap = NULL;
        int                      i = 0;
        struct mod_glfs_ai_item *p = &items[i];

        if ((tt = strrchr(path, '/')) == NULL) {
                tt = path;
        }
        else {
                tt++;
        }

        for (i = 0; i < list->nelts; ++i) {
                p = &items[i];
                if ((ap = strrchr(p->apply_to, '/')) == NULL) {
                        ap = p->apply_to;
                }
                else {
                        ap++;
                }

#ifndef CASE_BLIND_FILESYSTEM
                if (!ap_strcmp_match(path, p->apply_path)
                    && !ap_strcmp_match(tt, ap)) {
                        return 1;
                }
#else  /* !CASE_BLIND_FILESYSTEM */
                /*
                 * On some platforms, the match must be case-blind.  This is really
                 * a factor of the filesystem involved, but we can't detect that
                 * reliably - so we have to granularise at the OS level.
                 */
                if (!ap_strcasecmp_match(path, p->apply_path)
                    && !ap_strcasecmp_match(tt, ap)) {
                        return 1;
                }
#endif /* !CASE_BLIND_FILESYSTEM */
        }
        return 0;
}

/*****************************************************************
 *
 * Actually generating output
 */

/*
 * Elements of the emitted document:
 *      Preamble
 *              Emitted unless SUPPRESS_PREAMBLE is set AND ap_run_sub_req
 *              succeeds for the (content_type == text/html) header file.
 *      Header file
 *              Emitted if found (and able).
 *      H1 tag line
 *              Emitted if a header file is NOT emitted.
 *      Directory stuff
 *              Always emitted.
 *      HR
 *              Emitted if FANCY_INDEXING is set.
 *      Readme file
 *              Emitted if found (and able).
 *      ServerSig
 *              Emitted if ServerSignature is not Off AND a readme file
 *              is NOT emitted.
 *      Postamble
 *              Emitted unless SUPPRESS_PREAMBLE is set AND ap_run_sub_req
 *              succeeds for the (content_type == text/html) readme file.
 */


/*
 * emit a plain text file
 */
static void do_emit_plain(request_rec *r, apr_file_t *f)
{
        char         buf[AP_IOBUFSIZE + 1];
        int          ch = 0;
        apr_size_t   i = 0, c = 0, n = 0;
        apr_status_t rv = APR_SUCCESS;

        ap_rputs("<pre>\n", r);
        while (!apr_file_eof(f)) {
                do {
                        n = sizeof(char) * AP_IOBUFSIZE;
                        rv = apr_file_read(f, buf, &n);
                } while (APR_STATUS_IS_EINTR(rv));
                if (n == 0 || rv != APR_SUCCESS) {
                        /* ###: better error here? */
                        break;
                }
                buf[n] = '\0';
                c = 0;
                while (c < n) {
                        for (i = c; i < n; i++) {
                                if (buf[i] == '<' || buf[i] == '>'
                                    || buf[i] == '&') {
                                        break;
                                }
                        }
                        ch = buf[i];
                        buf[i] = '\0';
                        ap_rputs(&buf[c], r);
                        if (ch == '<') {
                                ap_rputs("&lt;", r);
                        }
                        else if (ch == '>') {
                                ap_rputs("&gt;", r);
                        }
                        else if (ch == '&') {
                                ap_rputs("&amp;", r);
                        }
                        c = i + 1;
                }
        }
        ap_rputs("</pre>\n", r);
}

/*
 * Handle the preamble through the H1 tag line, inclusive.  Locate
 * the file with a subrequests.  Process text/html documents by actually
 * running the subrequest; text/xxx documents get copied verbatim,
 * and any other content type is ignored.  This means that a non-text
 * document (such as HEADER.gif) might get multiviewed as the result
 * instead of a text document, meaning nothing will be displayed, but
 * oh well.
 */
static void emit_head(request_rec *r, char *header_fname, int suppress_amble,
                      int emit_xhtml, char *title)
{
        apr_table_t *hdrs = r->headers_in;
        apr_file_t  *f = NULL;
        request_rec *rr = NULL;
        int          emit_amble = 1;
        int          emit_H1 = 1;
        const char  *r_accept = NULL;
        const char  *r_accept_enc = NULL;

        /*
         * If there's a header file, send a subrequest to look for it.  If it's
         * found and html do the subrequest, otherwise handle it
         */
        r_accept = apr_table_get(hdrs, "Accept");
        r_accept_enc = apr_table_get(hdrs, "Accept-Encoding");
        apr_table_setn(hdrs, "Accept", "text/html, text/plain");
        apr_table_unset(hdrs, "Accept-Encoding");


        if ((header_fname != NULL) && r->args) {
                header_fname = apr_pstrcat(r->pool, header_fname, "?", r->args,
                                           NULL);
        }

        if ((header_fname != NULL)
            && (rr = ap_sub_req_lookup_uri(header_fname, r, r->output_filters))
            && (rr->status == HTTP_OK)
            && (rr->filename != NULL)
            && (rr->finfo.filetype == APR_REG)) {
                /*
                 * Check for the two specific cases we allow: text/html and
                 * text/anything-else.  The former is allowed to be processed for
                 * SSIs.
                 */
                if (rr->content_type != NULL) {
                        if (!strcasecmp(ap_field_noparam(r->pool,
                                                         rr->content_type),
                                        "text/html")) {
                                ap_filter_t *f = NULL;
        
                                /* Hope everything will work... */
                                emit_amble = 0;
                                emit_H1 = 0;

                                if (! suppress_amble) {
                                        emit_preamble(r, emit_xhtml, title);
                                }
                                /* This is a hack, but I can't find any better 
                                 * way to do this. The problem is that we have 
                                 * already created the sub-request,
                                 * but we just inserted the OLD_WRITE filter, 
                                 * and the sub-request needs to pass its data 
                                 * through the OLD_WRITE filter, or things go 
                                 * horribly wrong (missing data, data in
                                 * the wrong order, etc).  To fix it, if you 
                                 * create a sub-request and then insert the 
                                 * OLD_WRITE filter before you run the request, 
                                 * you need to make sure that the sub-request
                                 * data goes through the OLD_WRITE filter.  Just
                                 * steal this code.  The long-term solution is 
                                 * to remove the ap_r* functions.
                                 */
                                for (f=rr->output_filters;
                                     f->frec != ap_subreq_core_filter_handle;
                                     f = f->next);
                                f->next = r->output_filters;

                                /*
                                 * If there's a problem running the subrequest, 
                                 * display the preamble if we didn't do it 
                                 * before -- the header file didn't get displayed.
                                 */
                                if (ap_run_sub_req(rr) != OK) {
                                        /* It didn't work */
                                        emit_amble = suppress_amble;
                                        emit_H1 = 1;
                                }
                        }
                        else if (!strncasecmp("text/", rr->content_type, 5)) {
                                /*
                                 * If we can open the file, prefix it with the 
                                 * preamble regardless; since we'll be sending 
                                 * a <pre> block around the file's contents, 
                                 * any HTML header it had won't end up
                                 * where it belongs.
                                 */
                                if (apr_file_open(&f, rr->filename, APR_READ,
                                                  APR_OS_DEFAULT, r->pool) 
                                    == APR_SUCCESS) {
                                        emit_preamble(r, emit_xhtml, title);
                                        emit_amble = 0;
                                        do_emit_plain(r, f);
                                        apr_file_close(f);
                                        emit_H1 = 0;
                                }
                        }
                }
        }

        if (r_accept) {
                apr_table_setn(hdrs, "Accept", r_accept);
        }
        else {
                apr_table_unset(hdrs, "Accept");
        }

        if (r_accept_enc) {
                apr_table_setn(hdrs, "Accept-Encoding", r_accept_enc);
        }

        if (emit_amble) {
                emit_preamble(r, emit_xhtml, title);
        }
        if (emit_H1) {
                ap_rvputs(r, "<h1>Index of ", title, "</h1>\n", NULL);
        }
        if (rr != NULL) {
                ap_destroy_sub_req(rr);
        }
}


/*
 * Handle the Readme file through the postamble, inclusive.  Locate
 * the file with a subrequests.  Process text/html documents by actually
 * running the subrequest; text/xxx documents get copied verbatim,
 * and any other content type is ignored.  This means that a non-text
 * document (such as FOOTER.gif) might get multiviewed as the result
 * instead of a text document, meaning nothing will be displayed, but
 * oh well.
 */
static void emit_tail(request_rec *r, char *readme_fname, int suppress_amble)
{
        apr_file_t  *f = NULL;
        request_rec *rr = NULL;
        int          suppress_post = 0, suppress_sig = 0;

        /*
         * If there's a readme file, send a subrequest to look for it.  If it's
         * found and a text file, handle it -- otherwise fall through and
         * pretend there's nothing there.
         */
        if ((readme_fname != NULL)
            && (rr = ap_sub_req_lookup_uri(readme_fname, r, r->output_filters))
            && (rr->status == HTTP_OK)
            && (rr->filename != NULL)
            && rr->finfo.filetype == APR_REG) {
                /*
                 * Check for the two specific cases we allow: text/html and
                 * text/anything-else.  The former is allowed to be processed for
                 * SSIs.
                 */
                if (rr->content_type != NULL) {
                        if (!strcasecmp(ap_field_noparam(r->pool,
                                                         rr->content_type),
                                        "text/html")) {
                                ap_filter_t *f;
                                for (f=rr->output_filters;
                                     f->frec != ap_subreq_core_filter_handle;
                                     f = f->next);
                                f->next = r->output_filters;


                                if (ap_run_sub_req(rr) == OK) {
                                        /* worked... */
                                        suppress_sig = 1;
                                        suppress_post = suppress_amble;
                                }
                        }
                        else if (!strncasecmp("text/", rr->content_type, 5)) {
                                /*
                                 * If we can open the file, suppress the signature.
                                 */
                                if (apr_file_open(&f, rr->filename, APR_READ,
                                                  APR_OS_DEFAULT, r->pool)
                                    == APR_SUCCESS) {
                                        do_emit_plain(r, f);
                                        apr_file_close(f);
                                        suppress_sig = 1;
                                }
                        }
                }
        }

        if (!suppress_sig) {
                ap_rputs(ap_psignature("", r), r);
        }
        if (!suppress_post) {
                ap_rputs("</body></html>\n", r);
        }
        if (rr != NULL) {
                ap_destroy_sub_req(rr);
        }
}


static char *find_title(request_rec *r)
{
        char        titlebuf[MAX_STRING_LEN], *find = "<title>";
        apr_file_t *thefile = NULL;
        int         x = 0, y = 0, p = 0;
        apr_size_t  n;

        if (r->status != HTTP_OK) {
                return NULL;
        }
        if ((r->content_type != NULL)
            && (!strcasecmp(ap_field_noparam(r->pool, r->content_type),
                            "text/html")
                || !strcmp(r->content_type, INCLUDES_MAGIC_TYPE))
            && !r->content_encoding) {
                if (apr_file_open(&thefile, r->filename, APR_READ,
                                  APR_OS_DEFAULT, r->pool) != APR_SUCCESS) {
                        return NULL;
                }
                n = sizeof(char) * (MAX_STRING_LEN - 1);
                apr_file_read(thefile, titlebuf, &n);
                if (n <= 0) {
                        apr_file_close(thefile);
                        return NULL;
                }
                titlebuf[n] = '\0';
                for (x = 0, p = 0; titlebuf[x]; x++) {
                        if (apr_tolower(titlebuf[x]) == find[p]) {
                                if (!find[++p]) {
                                        if ((p = ap_ind(&titlebuf[++x], '<'))
                                            != -1) {
                                                titlebuf[x + p] = '\0';
                                        }
                                        /* Scan for line breaks for Tanmoy's 
                                           secretary
                                        */
                                        for (y = x; titlebuf[y]; y++) {
                                                if ((titlebuf[y] == CR) 
                                                    || (titlebuf[y] == LF)) {
                                                        if (y == x) {
                                                                x++;
                                                        }
                                                        else {
                                                                titlebuf[y] = ' ';
                                                        }
                                                }
                                        }
                                        apr_file_close(thefile);
                                        return apr_pstrdup(r->pool,
                                                           &titlebuf[x]);
                                }
                        }
                        else {
                                p = 0;
                        }
                }
                apr_file_close(thefile);
        }
        return NULL;
}

static struct ent *make_parent_entry(apr_int32_t autoindex_opts,
                                     glusterfs_dir_config_t *d,
                                     request_rec *r, char keyid,
                                     char direction)
{
        struct ent *p = NULL;
        char       *testpath = NULL;
        /*
         * p->name is now the true parent URI.
         * testpath is a crafted lie, so that the syntax '/some/..'
         * (or simply '..')be used to describe 'up' from '/some/'
         * when processeing IndexIgnore, and Icon|Alt|Desc configs.
         */

        p = (struct ent *) apr_pcalloc(r->pool, sizeof(struct ent));
        /* The output has always been to the parent.  Don't make ourself
         * our own parent (worthless cyclical reference).
         */
        if (!(p->name = ap_make_full_path(r->pool, r->uri, "../"))) {
                return (NULL);
        }
        ap_getparents(p->name);
        if (!*p->name) {
                return (NULL);
        }

        /* IndexIgnore has always compared "/thispath/.." */
        testpath = ap_make_full_path(r->pool, r->filename, "..");
        if (ignore_entry(d, testpath)) {
                return (NULL);
        }

        p->size = -1;
        p->lm = -1;
        p->key = apr_toupper(keyid);
        p->ascending = (apr_toupper(direction) == D_ASCENDING);
        p->version_sort = autoindex_opts & VERSION_SORT;
        if (autoindex_opts & FANCY_INDEXING) {
                if (!(p->icon = find_default_icon(d, testpath))) {
                        p->icon = find_default_icon(d, "^^DIRECTORY^^");
                }
                if (!(p->alt = find_default_alt(d, testpath))) {
                        if (!(p->alt = find_default_alt(d, "^^DIRECTORY^^"))) {
                                p->alt = "DIR";
                        }
                }
                p->desc = find_desc(d, testpath);
        }
        return p;
}

static struct ent *make_autoindex_entry(const apr_finfo_t *dirent,
                                        int autoindex_opts,
                                        glusterfs_dir_config_t *d,
                                        request_rec *r, char keyid,
                                        char direction,
                                        const char *pattern)
{
        request_rec *rr = NULL;
        struct ent  *p = NULL;
        int          show_forbidden = 0;

        /* Dot is ignored, Parent is handled by make_parent_entry() */
        if ((dirent->name[0] == '.') && (!dirent->name[1]
                                         || ((dirent->name[1] == '.')
                                             && !dirent->name[2])))
                return (NULL);

        /*
         * On some platforms, the match must be case-blind.  This is really
         * a factor of the filesystem involved, but we can't detect that
         * reliably - so we have to granularise at the OS level.
         */
        if (pattern && (apr_fnmatch(pattern, dirent->name,
                                    APR_FNM_NOESCAPE | APR_FNM_PERIOD
#ifdef CASE_BLIND_FILESYSTEM
                                    | APR_FNM_CASE_BLIND
#endif
                                )
                        != APR_SUCCESS)) {
                return (NULL);
        }

        if (ignore_entry(d, ap_make_full_path(r->pool,
                                              r->filename, dirent->name))) {
                return (NULL);
        }

        if (!(rr = ap_sub_req_lookup_dirent(dirent, r, AP_SUBREQ_NO_ARGS,
                                            NULL))) {
                return (NULL);
        }

        if((autoindex_opts & SHOW_FORBIDDEN)
           && (rr->status == HTTP_UNAUTHORIZED
               || rr->status == HTTP_FORBIDDEN)) {
                show_forbidden = 1;
        }

        if ((rr->finfo.filetype != APR_DIR && rr->finfo.filetype != APR_REG)
            || !(rr->status == OK || ap_is_HTTP_SUCCESS(rr->status)
                 || ap_is_HTTP_REDIRECT(rr->status)
                 || show_forbidden == 1)) {
                ap_destroy_sub_req(rr);
                return (NULL);
        }

        p = (struct ent *) apr_pcalloc(r->pool, sizeof(struct ent));
        if (dirent->filetype == APR_DIR) {
                p->name = apr_pstrcat(r->pool, dirent->name, "/", NULL);
        }
        else {
                p->name = apr_pstrdup(r->pool, dirent->name);
        }
        p->size = -1;
        p->icon = NULL;
        p->alt = NULL;
        p->desc = NULL;
        p->lm = -1;
        p->isdir = 0;
        p->key = apr_toupper(keyid);
        p->ascending = (apr_toupper(direction) == D_ASCENDING);
        p->version_sort = !!(autoindex_opts & VERSION_SORT);
        p->ignore_case = !!(autoindex_opts & IGNORE_CASE);

        if (autoindex_opts & (FANCY_INDEXING | TABLE_INDEXING)) {
                p->lm = rr->finfo.mtime;
                if (dirent->filetype == APR_DIR) {
                        if (autoindex_opts & FOLDERS_FIRST) {
                                p->isdir = 1;
                        }
                        rr->filename = ap_make_dirstr_parent (rr->pool,
                                                              rr->filename);

                        /* omit the trailing slash (1.3 compat) */
                        rr->filename[strlen(rr->filename) - 1] = '\0';

                        if (!(p->icon = find_icon(d, rr, 1))) {
                                p->icon = find_default_icon(d, "^^DIRECTORY^^");
                        }
                        if (!(p->alt = find_alt(d, rr, 1))) {
                                if (!(p->alt = find_default_alt(d,
                                                                "^^DIRECTORY^^"))) {
                                        p->alt = "DIR";
                                }
                        }
                }
                else {
                        p->icon = find_icon(d, rr, 0);
                        p->alt = find_alt(d, rr, 0);
                        p->size = rr->finfo.size;
                }

                p->desc = find_desc(d, rr->filename);

                if ((!p->desc) && (autoindex_opts & SCAN_HTML_TITLES)) {
                        p->desc = apr_pstrdup(r->pool, find_title(rr));
                }
        }
        ap_destroy_sub_req(rr);
        /*
         * We don't need to take any special action for the file size key.
         * If we did, it would go here.
         */
        if (keyid == K_LAST_MOD) {
                if (p->lm < 0) {
                        p->lm = 0;
                }
        }
        return (p);
}

static char *terminate_description(glusterfs_dir_config_t *d, char *desc,
                                   apr_int32_t autoindex_opts, int desc_width)
{
        int          maxsize = desc_width;
        register int x = 0;

        /*
         * If there's no DescriptionWidth in effect, default to the old
         * behaviour of adjusting the description size depending upon
         * what else is being displayed.  Otherwise, stick with the
         * setting.
         */
        if (d->desc_adjust == K_UNSET) {
                if (autoindex_opts & SUPPRESS_ICON) {
                        maxsize += 6;
                }
                if (autoindex_opts & SUPPRESS_LAST_MOD) {
                        maxsize += 19;
                }
                if (autoindex_opts & SUPPRESS_SIZE) {
                        maxsize += 7;
                }
        }
        for (x = 0; desc[x] && ((maxsize > 0) || (desc[x] == '<')); x++) {
                if (desc[x] == '<') {
                        while (desc[x] != '>') {
                                if (!desc[x]) {
                                        maxsize = 0;
                                        break;
                                }
                                ++x;
                        }
                }
                else if (desc[x] == '&') {
                        /* entities like &auml; count as one character */
                        --maxsize;
                        for ( ; desc[x] != ';'; ++x) {
                                if (desc[x] == '\0') {
                                        maxsize = 0;
                                        break;
                                }
                        }
                }
                else {
                        --maxsize;
                }
        }
        if (!maxsize && desc[x] != '\0') {
                desc[x - 1] = '>';      /* Grump. */
                desc[x] = '\0';         /* Double Grump! */
        }
        return desc;
}

/*
 * Emit the anchor for the specified field.  If a field is the key for the
 * current request, the link changes its meaning to reverse the order when
 * selected again.  Non-active fields always start in ascending order.
 */
static void emit_link(request_rec *r, const char *anchor, char column,
                      char curkey, char curdirection,
                      const char *colargs, int nosort)
{
        char qvalue[9];

        if (!nosort) {

                qvalue[0] = '?';
                qvalue[1] = 'C';
                qvalue[2] = '=';
                qvalue[3] = column;
                qvalue[4] = ';';
                qvalue[5] = 'O';
                qvalue[6] = '=';
                /* reverse? */
                qvalue[7] = ((curkey == column) && (curdirection == D_ASCENDING))
                        ? D_DESCENDING : D_ASCENDING;
                qvalue[8] = '\0';
                ap_rvputs(r, "<a href=\"", qvalue, colargs ? colargs : "",
                          "\">", anchor, "</a>", NULL);
        }
        else {
                ap_rputs(anchor, r);
        }
}

static void output_directories(struct ent **ar, int n,
                               glusterfs_dir_config_t *d, request_rec *r,
                               apr_int32_t autoindex_opts, char keyid,
                               char direction, const char *colargs)
{
        int            x = 0;
        apr_size_t     rv = APR_SUCCESS;
        char          *name = NULL, *tp = NULL;
        int            static_columns = 0;
        apr_pool_t    *scratch = NULL;
        int            name_width = 0, desc_width = 0, cols = 0;
        char          *name_scratch = NULL, *pad_scratch = NULL, *breakrow = "";
        char          *anchor = NULL, *t = NULL, *t2 = NULL;
        int            nwidth = 0;
        char           time_str[MAX_STRING_LEN];
        apr_time_exp_t ts = {0, };
        char           buf[5];

        name = r->uri;
        static_columns = !!(autoindex_opts & SUPPRESS_COLSORT);
        apr_pool_create(&scratch, r->pool);
        if (name[0] == '\0') {
                name = "/";
        }

        name_width = d->name_width;
        desc_width = d->desc_width;

        if ((autoindex_opts & (FANCY_INDEXING | TABLE_INDEXING))
            == FANCY_INDEXING) {
                if (d->name_adjust == K_ADJUST) {
                        for (x = 0; x < n; x++) {
                                int t = 0;
                                t = strlen(ar[x]->name);
                                if (t > name_width) {
                                        name_width = t;
                                }
                        }
                }

                if (d->desc_adjust == K_ADJUST) {
                        for (x = 0; x < n; x++) {
                                if (ar[x]->desc != NULL) {
                                        int t = 0;
                                        t = strlen(ar[x]->desc);
                                        if (t > desc_width) {
                                                desc_width = t;
                                        }
                                }
                        }
                }
        }
        name_scratch = apr_palloc(r->pool, name_width + 1);
        pad_scratch = apr_palloc(r->pool, name_width + 1);
        memset(pad_scratch, ' ', name_width);
        pad_scratch[name_width] = '\0';

        if (autoindex_opts & TABLE_INDEXING) {
                cols = 1;
                ap_rputs("<table><tr>", r);
                if (!(autoindex_opts & SUPPRESS_ICON)) {
                        ap_rputs("<th>", r);
                        if ((tp = find_default_icon(d, "^^BLANKICON^^"))) {
                                ap_rvputs(r, "<img src=\"",
                                          ap_escape_html(scratch, tp),
                                          "\" alt=\"[ICO]\"", NULL);
                                if (d->icon_width) {
                                        ap_rprintf(r, " width=\"%d\"",
                                                   d->icon_width);
                                }
                                if (d->icon_height) {
                                        ap_rprintf(r, " height=\"%d\"",
                                                   d->icon_height);
                                }

                                if (autoindex_opts & EMIT_XHTML) {
                                        ap_rputs(" /", r);
                                }
                                ap_rputs("></th>", r);
                        }
                        else {
                                ap_rputs("&nbsp;</th>", r);
                        }

                        ++cols;
                }
                ap_rputs("<th>", r);
                emit_link(r, "Name", K_NAME, keyid, direction,
                          colargs, static_columns);
                if (!(autoindex_opts & SUPPRESS_LAST_MOD)) {
                        ap_rputs("</th><th>", r);
                        emit_link(r, "Last modified", K_LAST_MOD, keyid,
                                  direction, colargs, static_columns);
                        ++cols;
                }
                if (!(autoindex_opts & SUPPRESS_SIZE)) {
                        ap_rputs("</th><th>", r);
                        emit_link(r, "Size", K_SIZE, keyid, direction,
                                  colargs, static_columns);
                        ++cols;
                }
                if (!(autoindex_opts & SUPPRESS_DESC)) {
                        ap_rputs("</th><th>", r);
                        emit_link(r, "Description", K_DESC, keyid, direction,
                                  colargs, static_columns);
                        ++cols;
                }
                if (!(autoindex_opts & SUPPRESS_RULES)) {
                        breakrow = apr_psprintf(r->pool,
                                                "<tr><th colspan=\"%d\">"
                                                "<hr%s></th></tr>\n", cols,
                                                (autoindex_opts & EMIT_XHTML) 
                                                ? " /" : "");
                }
                ap_rvputs(r, "</th></tr>", breakrow, NULL);
        }
        else if (autoindex_opts & FANCY_INDEXING) {
                ap_rputs("<pre>", r);
                if (!(autoindex_opts & SUPPRESS_ICON)) {
                        if ((tp = find_default_icon(d, "^^BLANKICON^^"))) {
                                ap_rvputs(r, "<img src=\"",
                                          ap_escape_html(scratch, tp),
                                          "\" alt=\"Icon \"", NULL);
                                if (d->icon_width) {
                                        ap_rprintf(r, " width=\"%d\"",
                                                   d->icon_width);
                                }
                                if (d->icon_height) {
                                        ap_rprintf(r, " height=\"%d\"",
                                                   d->icon_height);
                                }

                                if (autoindex_opts & EMIT_XHTML) {
                                        ap_rputs(" /", r);
                                }
                                ap_rputs("> ", r);
                        }
                        else {
                                ap_rputs("      ", r);
                        }
                }
                emit_link(r, "Name", K_NAME, keyid, direction,
                          colargs, static_columns);
                ap_rputs(pad_scratch + 4, r);
                /*
                 * Emit the guaranteed-at-least-one-space-between-columns byte.
                 */
                ap_rputs(" ", r);
                if (!(autoindex_opts & SUPPRESS_LAST_MOD)) {
                        emit_link(r, "Last modified", K_LAST_MOD, keyid,
                                  direction, colargs, static_columns);
                        ap_rputs("      ", r);
                }
                if (!(autoindex_opts & SUPPRESS_SIZE)) {
                        emit_link(r, "Size", K_SIZE, keyid, direction,
                                  colargs, static_columns);
                        ap_rputs("  ", r);
                }
                if (!(autoindex_opts & SUPPRESS_DESC)) {
                        emit_link(r, "Description", K_DESC, keyid, direction,
                                  colargs, static_columns);
                }
                if (!(autoindex_opts & SUPPRESS_RULES)) {
                        ap_rputs("<hr", r);
                        if (autoindex_opts & EMIT_XHTML) {
                                ap_rputs(" /", r);
                        }
                        ap_rputs(">", r);
                }
                else {
                        ap_rputc('\n', r);
                }
        }
        else {
                ap_rputs("<ul>", r);
        }

        for (x = 0; x < n; x++) {
                apr_pool_clear(scratch);

                t = ar[x]->name;
                anchor = ap_escape_html(scratch, ap_os_escape_path(scratch, t,
                                                                   0));

                if (!x && t[0] == '/') {
                        t2 = "Parent Directory";
                }
                else {
                        t2 = t;
                }

                if (autoindex_opts & TABLE_INDEXING) {
                        ap_rputs("<tr>", r);
                        if (!(autoindex_opts & SUPPRESS_ICON)) {
                                ap_rputs("<td valign=\"top\">", r);
                                if (autoindex_opts & ICONS_ARE_LINKS) {
                                        ap_rvputs(r, "<a href=\"", anchor,
                                                  "\">", NULL);
                                }
                                if ((ar[x]->icon) || d->default_icon) {
                                        ap_rvputs(r, "<img src=\"",
                                                  ap_escape_html(scratch,
                                                                 ar[x]->icon ?
                                                                 ar[x]->icon
                                                                 : d->default_icon),
                                                  "\" alt=\"[",
                                                  (ar[x]->alt ? 
                                                   ar[x]->alt : "   "),
                                                  "]\"", NULL);
                                        if (d->icon_width) {
                                                ap_rprintf(r, " width=\"%d\"",
                                                           d->icon_width);
                                        }
                                        if (d->icon_height) {
                                                ap_rprintf(r, " height=\"%d\"",
                                                           d->icon_height);
                                        }

                                        if (autoindex_opts & EMIT_XHTML) {
                                                ap_rputs(" /", r);
                                        }
                                        ap_rputs(">", r);
                                }
                                else {
                                        ap_rputs("&nbsp;", r);
                                }
                                if (autoindex_opts & ICONS_ARE_LINKS) {
                                        ap_rputs("</a></td>", r);
                                }
                                else {
                                        ap_rputs("</td>", r);
                                }
                        }
                        if (d->name_adjust == K_ADJUST) {
                                ap_rvputs(r, "<td><a href=\"", anchor, "\">",
                                          ap_escape_html(scratch, t2), "</a>",
                                          NULL);
                        }
                        else {
                                nwidth = strlen(t2);
                                if (nwidth > name_width) {
                                        memcpy(name_scratch, t2, name_width - 3);
                                        name_scratch[name_width - 3] = '.';
                                        name_scratch[name_width - 2] = '.';
                                        name_scratch[name_width - 1] = '>';
                                        name_scratch[name_width] = 0;
                                        t2 = name_scratch;
                                        nwidth = name_width;
                                }
                                ap_rvputs(r, "<td><a href=\"", anchor, "\">",
                                          ap_escape_html(scratch, t2),
                                          "</a>", pad_scratch + nwidth, NULL);
                        }
                        if (!(autoindex_opts & SUPPRESS_LAST_MOD)) {
                                if (ar[x]->lm != -1) {
                                        char time_str[MAX_STRING_LEN];
                                        apr_time_exp_t ts;
                                        apr_time_exp_lt(&ts, ar[x]->lm);
                                        apr_strftime(time_str, &rv,
                                                     MAX_STRING_LEN,
                                                     "</td><td align=\"right\""
                                                     ">%d-%b-%Y %H:%M  ",
                                                     &ts);
                                        ap_rputs(time_str, r);
                                }
                                else {
                                        ap_rputs("</td><td>&nbsp;", r);
                                }
                        }
                        if (!(autoindex_opts & SUPPRESS_SIZE)) {
                                ap_rvputs(r, "</td><td align=\"right\">",
                                          apr_strfsize(ar[x]->size, buf), NULL);
                        }
                        if (!(autoindex_opts & SUPPRESS_DESC)) {
                                if (ar[x]->desc) {
                                        if (d->desc_adjust == K_ADJUST) {
                                                ap_rvputs(r, "</td><td>",
                                                          ar[x]->desc, NULL);
                                        }
                                        else {
                                                ap_rvputs(r, "</td><td>",
                                                          terminate_description(d, ar[x]->desc,
                                                                                autoindex_opts,
                                                                                desc_width), NULL);
                                        }
                                }
                        }
                        else {
                                ap_rputs("</td><td>&nbsp;", r);
                        }
                        ap_rputs("</td></tr>\n", r);
                }
                else if (autoindex_opts & FANCY_INDEXING) {
                        if (!(autoindex_opts & SUPPRESS_ICON)) {
                                if (autoindex_opts & ICONS_ARE_LINKS) {
                                        ap_rvputs(r, "<a href=\"", anchor,
                                                  "\">", NULL);
                                }
                                if ((ar[x]->icon) || d->default_icon) {
                                        ap_rvputs(r, "<img src=\"",
                                                  ap_escape_html(scratch,
                                                                 ar[x]->icon ?
                                                                 ar[x]->icon
                                                                 : d->default_icon),
                                                  "\" alt=\"[",
                                                  (ar[x]->alt ? ar[x]->alt 
                                                   : "   "),
                                                  "]\"", NULL);
                                        if (d->icon_width) {
                                                ap_rprintf(r, " width=\"%d\"",
                                                           d->icon_width);
                                        }
                                        if (d->icon_height) {
                                                ap_rprintf(r, " height=\"%d\"",
                                                           d->icon_height);
                                        }

                                        if (autoindex_opts & EMIT_XHTML) {
                                                ap_rputs(" /", r);
                                        }
                                        ap_rputs(">", r);
                                }
                                else {
                                        ap_rputs("     ", r);
                                }
                                if (autoindex_opts & ICONS_ARE_LINKS) {
                                        ap_rputs("</a> ", r);
                                }
                                else {
                                        ap_rputc(' ', r);
                                }
                        }
                        nwidth = strlen(t2);
                        if (nwidth > name_width) {
                                memcpy(name_scratch, t2, name_width - 3);
                                name_scratch[name_width - 3] = '.';
                                name_scratch[name_width - 2] = '.';
                                name_scratch[name_width - 1] = '>';
                                name_scratch[name_width] = 0;
                                t2 = name_scratch;
                                nwidth = name_width;
                        }
                        ap_rvputs(r, "<a href=\"", anchor, "\">",
                                  ap_escape_html(scratch, t2),
                                  "</a>", pad_scratch + nwidth, NULL);
                        /*
                         * The blank before the storm.. er, before the next
                         * field.
                         */
                        ap_rputs(" ", r);
                        if (!(autoindex_opts & SUPPRESS_LAST_MOD)) {
                                if (ar[x]->lm != -1) {
                                        apr_time_exp_lt(&ts, ar[x]->lm);
                                        apr_strftime(time_str, &rv,
                                                     MAX_STRING_LEN,
                                                     "%d-%b-%Y %H:%M  ", &ts);
                                        ap_rputs(time_str, r);
                                }
                                else {
                                        /* Length="22-Feb-1998 23:42  " 
                                         * (see 4 lines above)
                                         */
                                        ap_rputs("                   ", r);
                                }
                        }
                        if (!(autoindex_opts & SUPPRESS_SIZE)) {
                                ap_rputs(apr_strfsize(ar[x]->size, buf), r);
                                ap_rputs("  ", r);
                        }
                        if (!(autoindex_opts & SUPPRESS_DESC)) {
                                if (ar[x]->desc) {
                                        ap_rputs(terminate_description(d,
                                                                       ar[x]->desc,
                                                                       autoindex_opts,
                                                                       desc_width), r);
                                }
                        }
                        ap_rputc('\n', r);
                }
                else {
                        ap_rvputs(r, "<li><a href=\"", anchor, "\"> ",
                                  ap_escape_html(scratch, t2),
                                  "</a></li>\n", NULL);
                }
        }
        if (autoindex_opts & TABLE_INDEXING) {
                ap_rvputs(r, breakrow, "</table>\n", NULL);
        }
        else if (autoindex_opts & FANCY_INDEXING) {
                if (!(autoindex_opts & SUPPRESS_RULES)) {
                        ap_rputs("<hr", r);
                        if (autoindex_opts & EMIT_XHTML) {
                                ap_rputs(" /", r);
                        }
                        ap_rputs("></pre>\n", r);
                }
                else {
                        ap_rputs("</pre>\n", r);
                }
        }
        else {
                ap_rputs("</ul>\n", r);
        }
}

/*
 * Compare two file entries according to the sort criteria.  The return
 * is essentially a signum function value.
 */

static int dsortf(struct ent **e1, struct ent **e2)
{
        struct ent *c1 = NULL, *c2 = NULL;
        int         result = 0;

        /*
         * First, see if either of the entries is for the parent directory.
         * If so, that *always* sorts lower than anything else.
         */
        if ((*e1)->name[0] == '/') {
                return -1;
        }
        if ((*e2)->name[0] == '/') {
                return 1;
        }
        /*
         * Now see if one's a directory and one isn't, if we're set
         * isdir for FOLDERS_FIRST.
         */
        if ((*e1)->isdir != (*e2)->isdir) {
                return (*e1)->isdir ? -1 : 1;
        }
        /*
         * All of our comparisons will be of the c1 entry against the c2 one,
         * so assign them appropriately to take care of the ordering.
         */
        if ((*e1)->ascending) {
                c1 = *e1;
                c2 = *e2;
        }
        else {
                c1 = *e2;
                c2 = *e1;
        }

        switch (c1->key) {
        case K_LAST_MOD:
                if (c1->lm > c2->lm) {
                        return 1;
                }
                else if (c1->lm < c2->lm) {
                        return -1;
                }
                break;
        case K_SIZE:
                if (c1->size > c2->size) {
                        return 1;
                }
                else if (c1->size < c2->size) {
                        return -1;
                }
                break;
        case K_DESC:
                if (c1->version_sort) {
                        result = apr_strnatcmp(c1->desc ? c1->desc : "",
                                               c2->desc ? c2->desc : "");
                }
                else {
                        result = strcmp(c1->desc ? c1->desc : "",
                                        c2->desc ? c2->desc : "");
                }
                if (result) {
                        return result;
                }
                break;
        }

        /* names may identical when treated case-insensitively,
         * so always fall back on strcmp() flavors to put entries
         * in deterministic order.  This means that 'ABC' and 'abc'
         * will always appear in the same order, rather than
         * variably between 'ABC abc' and 'abc ABC' order.
         */

        if (c1->version_sort) {
                if (c1->ignore_case) {
                        result = apr_strnatcasecmp (c1->name, c2->name);
                }
                if (!result) {
                        result = apr_strnatcmp(c1->name, c2->name);
                }
        }

        /* The names may be identical in respects other other than
         * filename case when strnatcmp is used above, so fall back
         * to strcmp on conflicts so that fn1.01.zzz and fn1.1.zzz
         * are also sorted in a deterministic order.
         */

        if (!result && c1->ignore_case) {
                result = strcasecmp (c1->name, c2->name);
        }

        if (!result) {
                result = strcmp (c1->name, c2->name);
        }

        return result;
}


static int 
mod_glfs_index_directory (request_rec *r,
                          glusterfs_dir_config_t *autoindex_conf)
{
        char                   *title_name = NULL, *title_endp = NULL;
        char                   *pstring = NULL, *colargs = NULL;
        char                   *path = NULL, *fname = NULL, *charset = NULL;
        char                   *fullpath = NULL, *name = NULL, *ctype = NULL;
        apr_finfo_t             dirent;
        glusterfs_file_t        fd = NULL;
        apr_status_t            status = APR_SUCCESS;
        int                     num_ent = 0, x;
        struct ent             *head = NULL, *p = NULL;
        struct ent            **ar = NULL;
        const char             *qstring = NULL;
        apr_int32_t             autoindex_opts = autoindex_conf->opts;
        char                    keyid, direction;
        apr_size_t              dirpathlen;
        glusterfs_dir_config_t *dir_config = NULL;
        int                     ret = -1;
        struct dirent           entry = {0, };
        struct stat             st = {0, };

        name = r->filename;
        title_name = ap_escape_html(r->pool, r->uri);
        ctype = "text/html";
        dir_config = mod_glfs_dconfig (r);
        if (dir_config == NULL)  {
                return HTTP_INTERNAL_SERVER_ERROR;
        }

        path = r->uri;
        fd = glusterfs_open (path, O_RDONLY, 0);
        if (fd == 0) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                              "file permissions deny server access: %s",
                              r->filename);
                return HTTP_FORBIDDEN;
        }

        if (autoindex_conf->ctype) {
                ctype = autoindex_conf->ctype;
        }
        if (autoindex_conf->charset) {
                charset = autoindex_conf->charset;
        }
        else {
#if APR_HAS_UNICODE_FS
                charset = "UTF-8";
#else
                charset = "ISO-8859-1";
#endif
        }
        if (*charset) {
                ap_set_content_type(r, apr_pstrcat(r->pool, ctype, ";charset=",
                                                   charset, NULL));
        }
        else {
                ap_set_content_type(r, ctype);
        }

        if (autoindex_opts & TRACK_MODIFIED) {
                ap_update_mtime(r, r->finfo.mtime);
                ap_set_last_modified(r);
                ap_set_etag(r);
        }
        if (r->header_only) {
                glusterfs_close (fd);
                return 0;
        }

        /*
         * If there is no specific ordering defined for this directory,
         * default to ascending by filename.
         */
        keyid = autoindex_conf->default_keyid
                ? autoindex_conf->default_keyid : K_NAME;
        direction = autoindex_conf->default_direction
                ? autoindex_conf->default_direction : D_ASCENDING;

        /*
         * Figure out what sort of indexing (if any) we're supposed to use.
         *
         * If no QUERY_STRING was specified or client query strings have been
         * explicitly disabled.
         * If we are ignoring the client, suppress column sorting as well.
         */
        if (autoindex_opts & IGNORE_CLIENT) {
                qstring = NULL;
                autoindex_opts |= SUPPRESS_COLSORT;
                colargs = "";
        }
        else {
                char fval[5], vval[5], *ppre = "", *epattern = "";
                fval[0] = '\0'; vval[0] = '\0';
                qstring = r->args;

                while (qstring && *qstring) {

                        /* C= First Sort key Column (N, M, S, D) */
                        if (   qstring[0] == 'C' && qstring[1] == '='
                               && qstring[2] && strchr(K_VALID, qstring[2])
                               && (   qstring[3] == '&' || qstring[3] == ';'
                                      || !qstring[3])) {
                                keyid = qstring[2];
                                qstring += qstring[3] ? 4 : 3;
                        }

                        /* O= Sort order (A, D) */
                        else if (   qstring[0] == 'O' && qstring[1] == '='
                                    && (   (qstring[2] == D_ASCENDING)
                                           || (qstring[2] == D_DESCENDING))
                                    && (   qstring[3] == '&' 
                                           || qstring[3] == ';'
                                           || !qstring[3])) {
                                direction = qstring[2];
                                qstring += qstring[3] ? 4 : 3;
                        }

                        /* F= Output Format (0 plain, 1 fancy (pre), 2 table) */
                        else if (   qstring[0] == 'F' && qstring[1] == '='
                                    && qstring[2] && strchr("012", qstring[2])
                                    && (   qstring[3] == '&' || qstring[3] == ';'
                                           || !qstring[3])) {
                                if (qstring[2] == '0') {
                                        autoindex_opts &= ~(FANCY_INDEXING
                                                            | TABLE_INDEXING);
                                }
                                else if (qstring[2] == '1') {
                                        autoindex_opts = (autoindex_opts
                                                          | FANCY_INDEXING)
                                                & ~TABLE_INDEXING;
                                }
                                else if (qstring[2] == '2') {
                                        autoindex_opts |= FANCY_INDEXING
                                                | TABLE_INDEXING;
                                }
                                strcpy(fval, ";F= ");
                                fval[3] = qstring[2];
                                qstring += qstring[3] ? 4 : 3;
                        }

                        /* V= Version sort (0, 1) */
                        else if (   qstring[0] == 'V' && qstring[1] == '='
                                    && (qstring[2] == '0' || qstring[2] == '1')
                                    && (   qstring[3] == '&' || qstring[3] == ';'
                                           || !qstring[3])) {
                                if (qstring[2] == '0') {
                                        autoindex_opts &= ~VERSION_SORT;
                                }
                                else if (qstring[2] == '1') {
                                        autoindex_opts |= VERSION_SORT;
                                }
                                strcpy(vval, ";V= ");
                                vval[3] = qstring[2];
                                qstring += qstring[3] ? 4 : 3;
                        }

                        /* P= wildcard pattern (*.foo) */
                        else if (qstring[0] == 'P' && qstring[1] == '=') {
                                const char *eos = qstring += 2; /* for efficiency */

                                while (*eos && *eos != '&' && *eos != ';') {
                                        ++eos;
                                }

                                if (eos == qstring) {
                                        pstring = NULL;
                                }
                                else {
                                        pstring = apr_pstrndup(r->pool, qstring,
                                                               eos - qstring);
                                        if (ap_unescape_url(pstring) != OK) {
                                                /* ignore the pattern, if it's bad. */
                                                pstring = NULL;
                                        }
                                        else {
                                                ppre = ";P=";
                                                /* be correct */
                                                epattern = ap_escape_uri(r->pool,
                                                                         pstring);
                                        }
                                }

                                if (*eos && *++eos) {
                                        qstring = eos;
                                }
                                else {
                                        qstring = NULL;
                                }
                        }

                        /* Syntax error?  Ignore the remainder! */
                        else {
                                qstring = NULL;
                        }
                }
                colargs = apr_pstrcat(r->pool, fval, vval, ppre, epattern,
                                      NULL);
        }

        /* Spew HTML preamble */
        title_endp = title_name + strlen(title_name) - 1;

        while (title_endp > title_name && *title_endp == '/') {
                *title_endp-- = '\0';
        }

        emit_head(r, find_header(autoindex_conf, r),
                  autoindex_opts & SUPPRESS_PREAMBLE,
                  autoindex_opts & EMIT_XHTML, title_name);

        /*
         * Since we don't know how many dir. entries there are, put them into a
         * linked list and then arrayificate them so qsort can use them.
         */
        head = NULL;
        p = make_parent_entry(autoindex_opts, autoindex_conf, r, keyid,
                              direction);
        if (p != NULL) {
                p->next = head;
                head = p;
                num_ent++;
        }
        fullpath = apr_palloc(r->pool, APR_PATH_MAX);
        dirpathlen = strlen(name);
        memcpy(fullpath, name, dirpathlen);

        do {
                ret = glusterfs_readdir (fd, &entry, sizeof (entry));
                if (ret <= 0) {
                        break;
                }

                fname = apr_pstrcat (r->pool, path, entry.d_name, NULL);

                ret = glusterfs_stat (fname, &st);
                if (ret != 0) {
                        break;
                }
                
                dirent.fname = fname;
                dirent.name = apr_pstrdup (r->pool, entry.d_name);
                fill_out_finfo (&dirent, &st, 
                                APR_FINFO_MIN | APR_FINFO_IDENT
                                | APR_FINFO_NLINK | APR_FINFO_OWNER
                                | APR_FINFO_PROT);

                p = make_autoindex_entry(&dirent, autoindex_opts,
                                         autoindex_conf, r,
                                         keyid, direction, pstring);
                if (p != NULL) {
                        p->next = head;
                        head = p;
                        num_ent++;
                }
        } while (1);

        if (num_ent > 0) {
                ar = (struct ent **) apr_palloc(r->pool,
                                                num_ent * sizeof(struct ent *));
                p = head;
                x = 0;
                while (p) {
                        ar[x++] = p;
                        p = p->next;
                }

                qsort((void *) ar, num_ent, sizeof(struct ent *),
                      (int (*)(const void *, const void *)) dsortf);
        }
        output_directories(ar, num_ent, autoindex_conf, r, autoindex_opts,
                           keyid, direction, colargs);
        glusterfs_close (fd);

        emit_tail(r, find_readme(autoindex_conf, r),
                  autoindex_opts & SUPPRESS_PREAMBLE);

        return 0;
}


static int 
handle_autoindex(request_rec *r)
{
        glusterfs_dir_config_t *dir_config = NULL;
        int allow_opts;

        allow_opts = ap_allow_options(r);

        r->allowed |= (AP_METHOD_BIT << M_GET);
        if (r->method_number != M_GET) {
                return DECLINED;
        }

        dir_config = mod_glfs_dconfig (r);

        /* OK, nothing easy.  Trot out the heavy artillery... */

        if (allow_opts & OPT_INDEXES) {
                int errstatus;

                if ((errstatus = ap_discard_request_body(r)) != OK) {
                        return errstatus;
                }

                /* KLUDGE --- make the sub_req lookups happen in the right 
                 * directory. Fixing this in the sub_req_lookup functions 
                 * themselves is difficult, and would probably break 
                 * virtual includes...
                 */

                if (r->filename[strlen(r->filename) - 1] != '/') {
                        r->filename = apr_pstrcat(r->pool, r->filename, "/",
                                                  NULL);
                }
                return mod_glfs_index_directory(r, dir_config);
        } else {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                              "Directory index forbidden by "
                              "Options directive: %s", r->filename);
                return HTTP_FORBIDDEN;
        }
}


static int 
mod_glfs_handler (request_rec *r)
{
        conn_rec               *c = r->connection;
        apr_bucket_brigade     *bb;
        apr_bucket             *e;
        core_dir_config        *d;
        int                     errstatus;
        glusterfs_file_t        fd = NULL;
        apr_status_t            status;
        glusterfs_dir_config_t *dir_config = NULL;
        char                   *path = NULL;
        int                     num_ranges = 0;
        apr_size_t              size = 0;
        apr_off_t               range_start = 0, range_end = 0;
        char                   *current = NULL;
        apr_status_t            rv = 0;
        core_request_config    *req_cfg = NULL;

        /* XXX if/when somebody writes a content-md5 filter we either need to
         *     remove this support or coordinate when to use the filter vs.
         *     when to use this code
         *     The current choice of when to compute the md5 here matches the 1.3
         *     support fairly closely (unlike 1.3, we don't handle computing md5
         *     when the charset is translated).
         */

        int bld_content_md5;
        if (!r->handler || (r->handler
                            && strcmp (r->handler, GLUSTERFS_HANDLER)))
                return DECLINED;

        if (r->uri[0] == '\0') {
                return DECLINED;
        }
  
        if (r->finfo.filetype == APR_DIR) {
                return handle_autoindex (r);
        }

        dir_config = mod_glfs_dconfig (r);

        ap_allow_standard_methods(r, MERGE_ALLOW, M_GET, -1);
  
        /* We understood the (non-GET) method, but it might not be legal for
           this particular resource. Check to see if the 'deliver_script'
           flag is set. If so, then we go ahead and deliver the file since
           it isn't really content (only GET normally returns content).

           Note: based on logic further above, the only possible non-GET
           method at this point is POST. In the future, we should enable
           script delivery for all methods.  */
        if (r->method_number != M_GET) {
                req_cfg = ap_get_module_config(r->request_config, &core_module);
                if (!req_cfg->deliver_script) {
                        /* The flag hasn't been set for this request. Punt. */
                        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                                      "This resource does not accept the %s "
                                      "method.",
                                      r->method);
                        return HTTP_METHOD_NOT_ALLOWED;
                }
        }

        d = (core_dir_config *)ap_get_module_config(r->per_dir_config,
                                                    &core_module);
        bld_content_md5 = (d->content_md5 & 1)
                && r->output_filters->frec->ftype != AP_FTYPE_RESOURCE;

        if ((errstatus = ap_discard_request_body(r)) != OK) {
                return errstatus;
        }

        if (r->finfo.filetype == 0) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                              "File does not exist: %s", r->filename);
                return HTTP_NOT_FOUND;
        }

        if ((r->used_path_info != AP_REQ_ACCEPT_PATH_INFO) &&
            r->path_info && *r->path_info)
        {
                /* default to reject */
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                              "File does not exist: %s",
                              apr_pstrcat(r->pool, r->filename, r->path_info,
                                          NULL));
                return HTTP_NOT_FOUND;
        }

        ap_update_mtime (r, r->finfo.mtime);
        ap_set_last_modified (r);
        ap_set_etag (r);
        apr_table_setn (r->headers_out, "Accept-Ranges", "bytes");

        num_ranges = ap_set_byterange(r);
        if (num_ranges == 0) {
                size = r->finfo.size;
        } else {
                char *tmp = apr_pstrdup (r->pool, r->range);
                while ((current = ap_getword(r->pool, (const char **)&tmp, ','))
                       && (rv = parse_byterange(current, r->finfo.size,
                                                &range_start, &range_end))) {
                        size += (range_end - range_start);
                }
        }

        ap_set_content_length (r, size);
                        
        if ((errstatus = ap_meets_conditions(r)) != OK) {
                r->status = errstatus;
        }

        /* 
         * file is small enough to have already got the content in 
         * glusterfs_lookup
        */
        if (r->finfo.size <= dir_config->xattr_file_size && dir_config->buf) {
                if (bld_content_md5) {
                        apr_table_setn (r->headers_out, "Content-MD5",
                                        (const char *)ap_md5_binary(r->pool,
                                                                    dir_config->buf
                                                                    , r->finfo.size));
                }

                ap_log_rerror (APLOG_MARK, APLOG_NOTICE, 0, r, 
                               "fetching data from glusterfs through xattr "
                               "interface\n");
                
                bb = apr_brigade_create(r->pool, c->bucket_alloc);

                e = apr_bucket_heap_create (dir_config->buf, r->finfo.size,
                                            free, c->bucket_alloc);
                APR_BRIGADE_INSERT_TAIL (bb, e);
                
                e = apr_bucket_eos_create(c->bucket_alloc);
                APR_BRIGADE_INSERT_TAIL(bb, e);

                dir_config->buf = NULL;

                /* let the byterange_filter handle multipart requests */
                status = ap_pass_brigade(r->output_filters, bb);
                if (status == APR_SUCCESS
                    || r->status != HTTP_OK
                    || c->aborted) {
                        return OK;
                }
                else {
                        /* no way to know what type of error occurred */
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, status, r,
                                      "mod_glfs_handler: ap_pass_brigade "
                                      "returned %i",
                                      status);
                        return HTTP_INTERNAL_SERVER_ERROR;
                }
        }
        
        /* do standard open/read/close to fetch content */
        path = r->uri;
        
        fd = glusterfs_open (path, O_RDONLY, 0);
        if (fd == 0) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                              "file permissions deny server access: %s",
                              r->filename);
                return HTTP_FORBIDDEN;
        }

        /* 
         * byterange_filter cannot handle range requests, since we are not 
         * sending the whole data in a single brigade
         */


        if (num_ranges == 0) {
                mod_glfs_read_async (r, NULL, fd, 0, -1);
        } else {
                mod_glfs_handle_byte_ranges (r, fd, num_ranges);
        }
                
        glusterfs_close (fd);
}


#if 0
static apr_status_t 
mod_glfs_output_filter (ap_filter_t *f,
                        apr_bucket_brigade *b)
{
        size_t size = 0;
        apr_bucket_t *e = NULL; 
        size = atol (apr_table_get (r->notes, MOD_GLFS_SIZE));
        
        for (e = APR_BRIGADE_FIRST(b);
             e != APR_BRIGADE_SENTINEL(b);
             e = APR_BUCKET_NEXT(e))
        {
                /* FIXME: can there be more than one heap buckets? */
                if (e->type == &apr_bucket_type_heap) {
                        break;
                }
        }

        if (e != APR_BRIGADE_SENTINEL(b)) {
                e->length = size;
        }

        return ap_pass_brigade (f->next, b);
}
#endif

static int 
mod_glfs_fixup_dir(request_rec *r)
{
        glusterfs_dir_config_t  *d = NULL;
        char                    *dummy_ptr[1];
        char                   **names_ptr = NULL, *name_ptr = NULL;
        int                      num_names;
        int                      error_notfound = 0;
        char                    *ifile = NULL;
        request_rec             *rr = NULL;

        /* only handle requests against directories */
        if (r->finfo.filetype != APR_DIR) {
                return DECLINED;
        }

        if (!r->handler || strcmp (r->handler, GLUSTERFS_HANDLER)) {
                return DECLINED;
        }

        /* Never tolerate path_info on dir requests */
        if (r->path_info && *r->path_info) {
                return DECLINED;
        }

        d = (glusterfs_dir_config_t *)ap_get_module_config(r->per_dir_config,
                                                           &glusterfs_module);

        /* Redirect requests that are not '/' terminated */
        if (r->uri[0] == '\0' || r->uri[strlen(r->uri) - 1] != '/')
        {
                if (!d->do_slash) {
                        return DECLINED;
                }

                /* Only redirect non-get requests if we have no note to warn
                 * that this browser cannot handle redirs on non-GET requests
                 * (such as Microsoft's WebFolders).
                 */
                if ((r->method_number != M_GET)
                    && apr_table_get(r->subprocess_env, "redirect-carefully")) {
                        return DECLINED;
                }

                if (r->args != NULL) {
                        ifile = apr_pstrcat(r->pool, ap_escape_uri(r->pool,
                                                                   r->uri),
                                            "/", "?", r->args, NULL);
                }
                else {
                        ifile = apr_pstrcat(r->pool, ap_escape_uri(r->pool,
                                                                   r->uri),
                                            "/", NULL);
                }

                apr_table_setn(r->headers_out, "Location",
                               ap_construct_url(r->pool, ifile, r));
                return HTTP_MOVED_PERMANENTLY;
        }

        if (d->index_names) {
                names_ptr = (char **)d->index_names->elts;
                num_names = d->index_names->nelts;
        }
        else {
                dummy_ptr[0] = AP_DEFAULT_INDEX;
                names_ptr = dummy_ptr;
                num_names = 1;
        }

        for (; num_names; ++names_ptr, --num_names) {
                /* XXX: Is this name_ptr considered escaped yet, or not??? */
                name_ptr = *names_ptr;

                /* Once upon a time args were handled _after_ the successful
                 * redirect. But that redirect might then _refuse_ the 
                 * given r->args, creating a nasty tangle.  It seems safer to 
                 * consider the r->args while we determine if name_ptr is our 
                 * viable index, and therefore set them up correctly on redirect.
                 */
                if (r->args != NULL) {
                        name_ptr = apr_pstrcat(r->pool, name_ptr, "?", r->args,
                                               NULL);
                }

                rr = ap_sub_req_lookup_uri(name_ptr, r, NULL);

                /* The sub request lookup is very liberal, and the core 
                 * map_to_storage handler will almost always result in HTTP_OK 
                 * as /foo/index.html may be /foo with PATH_INFO="/index.html",
                 * or even / with PATH_INFO="/foo/index.html". To get around 
                 * this we insist that the the index be a regular filetype.
                 *
                 * Another reason is that the core handler also makes the 
                 * assumption that if r->finfo is still NULL by the time it 
                 * gets called, the file does not exist.
                 */
                if (rr->status == HTTP_OK
                    && (   (rr->handler && !strcmp(rr->handler, "proxy-server"))
                           || rr->finfo.filetype == APR_REG)) {
                        ap_internal_fast_redirect(rr, r);
                        return OK;
                }

                /* If the request returned a redirect, propagate it to the 
                 * client
                 */

                if (ap_is_HTTP_REDIRECT(rr->status)
                    || (rr->status == HTTP_NOT_ACCEPTABLE && num_names == 1)
                    || (rr->status == HTTP_UNAUTHORIZED && num_names == 1)) {

                        apr_pool_join(r->pool, rr->pool);
                        error_notfound = rr->status;
                        r->notes = apr_table_overlay(r->pool, r->notes,
                                                     rr->notes);
                        r->headers_out = apr_table_overlay(r->pool,
                                                           r->headers_out,
                                                           rr->headers_out);
                        r->err_headers_out = apr_table_overlay(r->pool,
                                                               r->err_headers_out,
                                                               rr->err_headers_out);
                        return error_notfound;
                }

                /* If the request returned something other than 404 (or 200),
                 * it means the module encountered some sort of problem. To be
                 * secure, we should return the error, rather than allow 
                 * autoindex to create a (possibly unsafe) directory index.
                 *
                 * So we store the error, and if none of the listed files
                 * exist, we return the last error response we got, instead
                 * of a directory listing.
                 */
                if (rr->status && rr->status != HTTP_NOT_FOUND
                    && rr->status != HTTP_OK) {
                        error_notfound = rr->status;
                }

                ap_destroy_sub_req(rr);
        }

        if (error_notfound) {
                return error_notfound;
        }

        /* nothing for us to do, pass on through */
        return DECLINED;
}


static void 
mod_glfs_register_hooks(apr_pool_t *p)
{
        ap_hook_child_init (mod_glfs_child_init, NULL, NULL, APR_HOOK_MIDDLE);
        ap_hook_handler (mod_glfs_handler, NULL, NULL, APR_HOOK_REALLY_FIRST);
        ap_hook_map_to_storage (mod_glfs_map_to_storage, NULL, NULL,
                                APR_HOOK_REALLY_FIRST);
        ap_hook_fixups(mod_glfs_fixup_dir,NULL,NULL,APR_HOOK_LAST);

/*    mod_glfs_output_filter_handle = 
      ap_register_output_filter ("MODGLFS", mod_glfs_output_filter,
      NULL, AP_FTYPE_PROTOCOL); */
}

static const char *
cmd_add_index (cmd_parms *cmd, void *dummy, const char *arg)
{
        glusterfs_dir_config_t *d = dummy;

        if (!d->index_names) {
                d->index_names = apr_array_make(cmd->pool, 2, sizeof(char *));
        }
        *(const char **)apr_array_push(d->index_names) = arg;
        return NULL;
}

static const char *
cmd_configure_slash (cmd_parms *cmd, void *d_, int arg)
{
        glusterfs_dir_config_t *d = d_;

        d->do_slash = arg ? SLASH_ON : SLASH_OFF;
        return NULL;
}

#define DIR_CMD_PERMS OR_INDEXES

static const 
command_rec mod_glfs_cmds[] =
{
        AP_INIT_TAKE1(
                "GlusterfsLogfile",
                cmd_add_logfile,
                NULL,
                ACCESS_CONF, /*FIXME: allow overriding in .htaccess files */
                "Glusterfs logfile"
                ),

        AP_INIT_TAKE1(
                "GlusterfsLoglevel",
                cmd_set_loglevel,
                NULL,
                ACCESS_CONF,
                "Glusterfs loglevel:anyone of none, critical, error, warning, "
                "debug"
                ),

        AP_INIT_TAKE1(
                "GlusterfsCacheTimeout",
                cmd_set_cache_timeout,
                NULL,
                ACCESS_CONF,
                "Timeout value in seconds for lookup and stat cache of "
                "libglusterfsclient"
                ),

        AP_INIT_TAKE1(
                "GlusterfsVolumeSpecfile",
                cmd_add_volume_specfile,
                NULL,
                ACCESS_CONF,
                "Glusterfs Volume specfication file specifying filesystem "
                "under this directory"
                ),

        AP_INIT_TAKE1(
                "GlusterfsXattrFileSize",
                cmd_add_xattr_file_size,
                NULL,
                ACCESS_CONF,
                "Maximum size of the file that can be fetched through "
                "extended attribute interface of libglusterfsclient"
                ),

        /* mod_dir cmds */
        AP_INIT_ITERATE("DirectoryIndex", cmd_add_index, 
                        NULL, DIR_CMD_PERMS,
                        "a list of file names"),

        AP_INIT_FLAG("DirectorySlash", cmd_configure_slash, 
                     NULL, DIR_CMD_PERMS,
                     "On or Off"),

        /* autoindex cmds */
        AP_INIT_ITERATE2("AddIcon", cmd_add_icon, 
                         BY_PATH, DIR_CMD_PERMS,
                         "an icon URL followed by one or more filenames"),

        AP_INIT_ITERATE2("AddIconByType", cmd_add_icon, 
                         BY_TYPE, DIR_CMD_PERMS,
                         "an icon URL followed by one or more MIME types"),

        AP_INIT_ITERATE2("AddIconByEncoding", cmd_add_icon, 
                         BY_ENCODING, DIR_CMD_PERMS,
                         "an icon URL followed by one or more content encodings"),

        AP_INIT_ITERATE2("AddAlt", cmd_add_alt, BY_PATH, 
                         DIR_CMD_PERMS,
                         "alternate descriptive text followed by one or more "
                         "filenames"),

        AP_INIT_ITERATE2("AddAltByType", cmd_add_alt, 
                         BY_TYPE, DIR_CMD_PERMS,
                         "alternate descriptive text followed by one or more "
                         "MIME types"),

        AP_INIT_ITERATE2("AddAltByEncoding", cmd_add_alt, 
                         BY_ENCODING, DIR_CMD_PERMS,
                         "alternate descriptive text followed by one or more "
                         "content encodings"),

        AP_INIT_TAKE_ARGV("IndexOptions", cmd_add_opts, 
                          NULL, DIR_CMD_PERMS,
                          "one or more index options [+|-][]"),

        AP_INIT_TAKE2("IndexOrderDefault", cmd_set_default_order, 
                      NULL, DIR_CMD_PERMS,
                      "{Ascending,Descending} {Name,Size,Description,Date}"),

        AP_INIT_ITERATE("IndexIgnore", cmd_add_ignore, 
                        NULL, DIR_CMD_PERMS,
                        "one or more file extensions"),

        AP_INIT_ITERATE2("AddDescription", cmd_add_desc, 
                         BY_PATH, DIR_CMD_PERMS,
                         "Descriptive text followed by one or more filenames"),

        AP_INIT_TAKE1("HeaderName", cmd_add_header, 
                      NULL, DIR_CMD_PERMS,
                      "a filename"),

        AP_INIT_TAKE1("ReadmeName", cmd_add_readme, 
                      NULL, DIR_CMD_PERMS,
                      "a filename"),

        AP_INIT_RAW_ARGS("FancyIndexing", ap_set_deprecated, 
                         NULL, OR_ALL,
                         "The FancyIndexing directive is no longer supported. "
                         "Use IndexOptions FancyIndexing."),

        AP_INIT_TAKE1("DefaultIcon", ap_set_string_slot,
                      (void *)APR_OFFSETOF(glusterfs_dir_config_t,
                                           default_icon),
                      DIR_CMD_PERMS, "an icon URL"),

        AP_INIT_TAKE1("IndexStyleSheet", ap_set_string_slot,
                      (void *)APR_OFFSETOF(glusterfs_dir_config_t, style_sheet),
                      DIR_CMD_PERMS, "URL to style sheet"),

        {NULL}
};

module AP_MODULE_DECLARE_DATA glusterfs_module =
{
        STANDARD20_MODULE_STUFF,
        mod_glfs_create_dir_config,
        mod_glfs_merge_dir_config,
        NULL, //mod_glfs_create_server_config,
        NULL, //mod_glfs_merge_server_config,
        mod_glfs_cmds,
        mod_glfs_register_hooks,
};
