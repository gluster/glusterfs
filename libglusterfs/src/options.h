/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "xlator.h"
#include "libglusterfs-messages.h"
/* Add possible new type of option you may need */
typedef enum {
        GF_OPTION_TYPE_ANY = 0,
        GF_OPTION_TYPE_STR,
        GF_OPTION_TYPE_INT,
        GF_OPTION_TYPE_SIZET,
        GF_OPTION_TYPE_PERCENT,
        GF_OPTION_TYPE_PERCENT_OR_SIZET,
        GF_OPTION_TYPE_BOOL,
        GF_OPTION_TYPE_XLATOR,
        GF_OPTION_TYPE_PATH,
        GF_OPTION_TYPE_TIME,
        GF_OPTION_TYPE_DOUBLE,
        GF_OPTION_TYPE_INTERNET_ADDRESS,
        GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
        GF_OPTION_TYPE_PRIORITY_LIST,
        GF_OPTION_TYPE_SIZE_LIST,
        GF_OPTION_TYPE_CLIENT_AUTH_ADDR,
        GF_OPTION_TYPE_MAX,
} volume_option_type_t;

typedef enum {
        GF_OPT_VALIDATE_BOTH = 0,
        GF_OPT_VALIDATE_MIN,
        GF_OPT_VALIDATE_MAX,
} opt_validate_type_t;

typedef enum {
        OPT_FLAG_NONE        = 0,
        OPT_FLAG_SETTABLE    = 1 << 0, /* can be set using volume set */
        OPT_FLAG_CLIENT_OPT  = 1 << 1, /* affects clients */
        OPT_FLAG_GLOBAL      = 1 << 2, /* affects all instances of the particular xlator */
        OPT_FLAG_FORCE       = 1 << 3, /* needs force to be reset */
        OPT_FLAG_NEVER_RESET = 1 << 4, /* which should not be reset */
        OPT_FLAG_DOC         = 1 << 5, /* can be shown in volume set help */
} opt_flags_t;


typedef enum {
        OPT_STATUS_ADVANCED       = 0,
        OPT_STATUS_BASIC          = 1,
        OPT_STATUS_EXPERIMENTAL   = 2,
        OPT_STATUS_DEPRECATED     = 3,
} opt_level_t;


#define ZR_VOLUME_MAX_NUM_KEY    4
#define ZR_OPTION_MAX_ARRAY_SIZE 64
/* The maximum number of releases that an option could be backported to
 * based on the release schedule as in August 2017 (3), plus one more
 * Refer comment on volume_options.op_version for more information.
 */
#define GF_MAX_RELEASES 4

/* Custom validation functoins for options
 * TODO: Need to check what sorts of validation is being done, and decide if
 * passing the volinfo is actually required. If it is, then we should possibly
 * try a solution in GD2 for this.
 */
/* typedef int (*option_validation_fn) (glusterd_volinfo_t *volinfo, dict_t *dict,
                                       char *key, char *value, char **op_errstr);
*/


/* Each translator should define this structure */
/* XXX: This structure is in use by GD2, and SHOULD NOT be modified.
 * If there is a need to add new members, add them to the end of the structure.
 * If the struct must be modified, GD2 MUST be updated as well
 */
typedef struct volume_options {
        char                    *key[ZR_VOLUME_MAX_NUM_KEY];
        /* different key, same meaning */
        volume_option_type_t    type;
        double                  min;  /* 0 means no range */
        double                  max;  /* 0 means no range */
        char                    *value[ZR_OPTION_MAX_ARRAY_SIZE];
        /* If specified, will check for one of
           the value from this array */
        char                    *default_value;
        char                    *description; /* about the key */
        /* Required for int options where only the min value
         * is given and is 0. This will cause validation not to
         * happen
         */
        opt_validate_type_t     validate;

        /* The op-version at which this option was introduced.
         * This is an array to support options that get backported to supported
         * releases.
         * Normally, an option introduced for a major release just has a single
         * entry in the array, with op-version of the major release
         * For an option that is backported, the op-versions of the all the
         * releases it was ported to should be added, starting from the newest,
         * to the oldest.
         */
        uint32_t op_version[GF_MAX_RELEASES];
        /* The op-version at which this option was deprecated.
         * Follows the same rules as above.
         */
        uint32_t deprecated[GF_MAX_RELEASES];
        /* Additional flags for an option
         * Check the OPT_FLAG_* enums for available flags
         */
        uint32_t flags;
        /* Tags applicable to this option, which can be used to group similar
         * options
         */
        char *tags[ZR_OPTION_MAX_ARRAY_SIZE];
        /* A custom validation function if required
         * TODO: See todo above for option_validation_fn
         */
        /* option_validation_fn validate_fn; */
        /* This is actual key that should be set in the options dict. Can
         * contain varstrings
         */
        char *setkey;

        /* The level at which the option is classified
         */
        opt_level_t             level;
} volume_option_t;


typedef struct vol_opt_list {
        struct list_head  list;
        volume_option_t  *given_opt;
} volume_opt_list_t;


int xlator_tree_reconfigure (xlator_t *old_xl, xlator_t *new_xl);
int xlator_validate_rec (xlator_t *xlator, char **op_errstr);
int graph_reconf_validateopt (glusterfs_graph_t *graph, char **op_errstr);
int xlator_option_info_list (volume_opt_list_t *list, char *key,
                             char **def_val, char **descr);
/*
int validate_xlator_volume_options (xlator_t *xl, dict_t *options,
                                    volume_option_t *opt, char **op_errstr);
*/
int xlator_options_validate_list (xlator_t *xl, dict_t *options,
                                  volume_opt_list_t *list, char **op_errstr);
int xlator_option_validate (xlator_t *xl, char *key, char *value,
                            volume_option_t *opt, char **op_errstr);
int xlator_options_validate (xlator_t *xl, dict_t *options, char **errstr);

int xlator_option_validate_addr_list (xlator_t *xl, const char *key,
                                      const char *value, volume_option_t *opt,
                                      char **op_errstr);

volume_option_t *
xlator_volume_option_get (xlator_t *xl, const char *key);

volume_option_t *
xlator_volume_option_get_list (volume_opt_list_t *vol_list, const char *key);


#define DECLARE_INIT_OPT(type_t, type)                                  \
int                                                                     \
xlator_option_init_##type (xlator_t *this, dict_t *options, char *key,  \
                           type_t *val_p);

DECLARE_INIT_OPT(char *, str);
DECLARE_INIT_OPT(uint64_t, uint64);
DECLARE_INIT_OPT(int64_t, int64);
DECLARE_INIT_OPT(uint32_t, uint32);
DECLARE_INIT_OPT(int32_t, int32);
DECLARE_INIT_OPT(size_t, size);
DECLARE_INIT_OPT(uint64_t, size_uint64);
DECLARE_INIT_OPT(double, percent);
DECLARE_INIT_OPT(double, percent_or_size);
DECLARE_INIT_OPT(gf_boolean_t, bool);
DECLARE_INIT_OPT(xlator_t *, xlator);
DECLARE_INIT_OPT(char *, path);
DECLARE_INIT_OPT(double, double);
DECLARE_INIT_OPT(uint32_t, time);


#define DEFINE_INIT_OPT(type_t, type, conv)                             \
int                                                                     \
xlator_option_init_##type (xlator_t *this, dict_t *options, char *key,  \
                           type_t *val_p)                               \
{                                                                       \
        int              ret = 0;                                       \
        volume_option_t *opt = NULL;                                    \
        char            *def_value = NULL;                              \
        char            *set_value = NULL;                              \
        char            *value = NULL;                                  \
        xlator_t        *old_THIS = NULL;                               \
                                                                        \
        opt = xlator_volume_option_get (this, key);                     \
        if (!opt) {                                                     \
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,             \
                        LG_MSG_INVALID_ENTRY,                           \
                        "unknown option: %s", key);                     \
                ret = -1;                                               \
                return ret;                                             \
        }                                                               \
        def_value = opt->default_value;                                 \
        ret = dict_get_str (options, key, &set_value);                  \
                                                                        \
        if (def_value)                                                  \
                value = def_value;                                      \
        if (set_value)                                                  \
                value = set_value;                                      \
        if (!value) {                                                   \
                gf_msg_trace (this->name, 0, "option %s not set",       \
                        key);                                           \
                *val_p = (type_t)0;                                     \
                return 0;                                               \
        }                                                               \
        if (value == def_value) {                                       \
                gf_msg_trace (this->name, 0, "option %s using default"  \
                              " value %s", key, value);                 \
        } else {                                                        \
                gf_msg_debug (this->name, 0, "option %s using set"      \
                              " value %s", key, value);                 \
        }                                                               \
        old_THIS = THIS;                                                \
        THIS = this;                                                    \
        ret = conv (value, val_p);                                      \
        THIS = old_THIS;                                                \
        if (ret) {							\
                gf_msg (this->name, GF_LOG_INFO, 0,                     \
                        LG_MSG_CONVERSION_FAILED,                       \
                        "option %s conversion failed value %s",         \
                        key, value);                                    \
                return ret;                                             \
	}                                                               \
        ret = xlator_option_validate (this, key, value, opt, NULL);     \
        return ret;                                                     \
}

#define GF_OPTION_INIT(key, val, type, err_label) do {            \
        int val_ret = 0;                                          \
        val_ret = xlator_option_init_##type (THIS, THIS->options, \
                                             key, &(val));        \
        if (val_ret)                                              \
                goto err_label;                                   \
        } while (0)



#define DECLARE_RECONF_OPT(type_t, type)                                \
int                                                                     \
xlator_option_reconf_##type (xlator_t *this, dict_t *options, char *key,\
                             type_t *val_p);

DECLARE_RECONF_OPT(char *, str);
DECLARE_RECONF_OPT(uint64_t, uint64);
DECLARE_RECONF_OPT(int64_t, int64);
DECLARE_RECONF_OPT(uint32_t, uint32);
DECLARE_RECONF_OPT(int32_t, int32);
DECLARE_RECONF_OPT(size_t, size);
DECLARE_RECONF_OPT(uint64_t, size_uint64);
DECLARE_RECONF_OPT(double, percent);
DECLARE_RECONF_OPT(double, percent_or_size);
DECLARE_RECONF_OPT(gf_boolean_t, bool);
DECLARE_RECONF_OPT(xlator_t *, xlator);
DECLARE_RECONF_OPT(char *, path);
DECLARE_RECONF_OPT(double, double);
DECLARE_RECONF_OPT(uint32_t, time);


#define DEFINE_RECONF_OPT(type_t, type, conv)                            \
int                                                                      \
xlator_option_reconf_##type (xlator_t *this, dict_t *options, char *key, \
                             type_t *val_p)                             \
{                                                                       \
        int              ret = 0;                                       \
        volume_option_t *opt = NULL;                                    \
        char            *def_value = NULL;                              \
        char            *set_value = NULL;                              \
        char            *value = NULL;                                  \
        xlator_t        *old_THIS = NULL;                               \
                                                                        \
        opt = xlator_volume_option_get (this, key);                     \
        if (!opt) {                                                     \
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,             \
                        LG_MSG_INVALID_ENTRY,                           \
                        "unknown option: %s", key);                     \
                ret = -1;                                               \
                return ret;                                             \
        }                                                               \
        def_value = opt->default_value;                                 \
        ret = dict_get_str (options, key, &set_value);                  \
                                                                        \
        if (def_value)                                                  \
                value = def_value;                                      \
        if (set_value)                                                  \
                value = set_value;                                      \
        if (!value) {                                                   \
                gf_msg_trace (this->name, 0, "option %s not set", key); \
                *val_p = (type_t)0;                                     \
                return 0;                                               \
        }                                                               \
        if (value == def_value) {                                       \
                gf_msg_trace (this->name, 0,                            \
                        "option %s using default value %s",             \
                        key, value);                                    \
        } else {                                                        \
                gf_msg (this->name, GF_LOG_INFO, 0, 0,                  \
                        "option %s using set value %s",                 \
                        key, value);                                    \
        }                                                               \
        old_THIS = THIS;                                                \
        THIS = this;                                                    \
        ret = conv (value, val_p);                                      \
        THIS = old_THIS;                                                \
        if (ret)                                                        \
                return ret;                                             \
        ret = xlator_option_validate (this, key, value, opt, NULL);     \
        return ret;                                                     \
}

#define GF_OPTION_RECONF(key, val, opt, type, err_label) do {       \
        int val_ret = 0;                                            \
        val_ret = xlator_option_reconf_##type (THIS, opt, key,      \
                                               &(val));             \
        if (val_ret)                                                \
                goto err_label;                                     \
        } while (0)


#endif /* !_OPTIONS_H */
