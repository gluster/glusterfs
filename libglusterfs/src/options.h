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

#define ZR_VOLUME_MAX_NUM_KEY    4
#define ZR_OPTION_MAX_ARRAY_SIZE 64

/* Each translator should define this structure */
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
                gf_msg_debug (this->name, 0,                            \
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
