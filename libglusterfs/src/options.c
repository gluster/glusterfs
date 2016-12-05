/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <fnmatch.h>

#include "xlator.h"
#include "defaults.h"
#include "libglusterfs-messages.h"

#define GF_OPTION_LIST_EMPTY(_opt) (_opt->value[0] == NULL)


static int
xlator_option_validate_path (xlator_t *xl, const char *key, const char *value,
                             volume_option_t *opt, char **op_errstr)
{
        int   ret = -1;
        char  errstr[256];

        if (strstr (value, "../")) {
                snprintf (errstr, 256,
                          "invalid path given '%s'",
                          value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

                /* Make sure the given path is valid */
        if (value[0] != '/') {
                snprintf (errstr, 256,
                          "option %s %s: '%s' is not an "
                          "absolute path name",
                          key, value, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}

static int
xlator_option_validate_int (xlator_t *xl, const char *key, const char *value,
                            volume_option_t *opt, char **op_errstr)
{
        long long inputll = 0;
        unsigned long long uinputll = 0;
        int       ret = -1;
        char      errstr[256];

        /* Check the range */
        if (gf_string2longlong (value, &inputll) != 0) {
                snprintf (errstr, 256,
                          "invalid number format \"%s\" in option \"%s\"",
                          value, key);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        /* Handle '-0' */
        if ((inputll == 0) && (gf_string2ulonglong (value, &uinputll) != 0)) {
                snprintf (errstr, 256,
                          "invalid number format \"%s\" in option \"%s\"",
                          value, key);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        if ((opt->min == 0) && (opt->max == 0) &&
            (opt->validate == GF_OPT_VALIDATE_BOTH)) {
                gf_msg_trace (xl->name, 0, "no range check required for "
                              "'option %s %s'", key, value);
                ret = 0;
                goto out;
        }

        if (opt->validate == GF_OPT_VALIDATE_MIN) {
                if (inputll < opt->min) {
                        snprintf (errstr, 256,
                                  "'%lld' in 'option %s %s' is smaller than "
                                  "minimum value '%.0f'", inputll, key,
                                  value, opt->min);
                        gf_msg (xl->name, GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_ENTRY, "%s", errstr);
                        goto out;
                }
        } else if (opt->validate == GF_OPT_VALIDATE_MAX) {
                if (inputll > opt->max) {
                        snprintf (errstr, 256,
                                  "'%lld' in 'option %s %s' is greater than "
                                  "maximum value '%.0f'", inputll, key,
                                  value, opt->max);
                        gf_msg (xl->name, GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_ENTRY, "%s", errstr);
                        goto out;
                }
        } else if ((inputll < opt->min) || (inputll > opt->max)) {
                snprintf (errstr, 256,
                          "'%lld' in 'option %s %s' is out of range "
                          "[%.0f - %.0f]",
                          inputll, key, value, opt->min, opt->max);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_OUT_OF_RANGE, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}


static int
xlator_option_validate_sizet (xlator_t *xl, const char *key, const char *value,
                              volume_option_t *opt, char **op_errstr)
{
        size_t  size = 0;
        int       ret = 0;
        char      errstr[256];

        /* Check the range */
        if (gf_string2bytesize_size (value, &size) != 0) {
                snprintf (errstr, 256,
                          "invalid number format \"%s\" in option \"%s\"",
                          value, key);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                ret = -1;
                goto out;
        }

        if ((opt->min == 0) && (opt->max == 0)) {
                gf_msg_trace (xl->name, 0, "no range check required for "
                              "'option %s %s'", key, value);
                goto out;
        }

        if ((size < opt->min) || (size > opt->max)) {
                if ((strncmp (key, "cache-size", 10) == 0) &&
                    (size > opt->max)) {
                       snprintf (errstr, 256, "Cache size %" GF_PRI_SIZET " is out of "
                                 "range [%.0f - %.0f]",
                                 size, opt->min, opt->max);
                       gf_msg (xl->name, GF_LOG_WARNING, 0,
                               LG_MSG_OUT_OF_RANGE, "%s", errstr);
                } else {
                        snprintf (errstr, 256,
                                  "'%" GF_PRI_SIZET "' in 'option %s %s' "
                                  "is out of range [%.0f - %.0f]",
                                  size, key, value, opt->min, opt->max);
                        gf_msg (xl->name, GF_LOG_ERROR, 0,
                                LG_MSG_OUT_OF_RANGE, "%s", errstr);
                        ret = -1;
                }
        }

out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}


static int
xlator_option_validate_bool (xlator_t *xl, const char *key, const char *value,
                             volume_option_t *opt, char **op_errstr)
{
        int          ret = -1;
        char         errstr[256];
        gf_boolean_t is_valid;


        /* Check if the value is one of
           '0|1|on|off|no|yes|true|false|enable|disable' */

        if (gf_string2boolean (value, &is_valid) != 0) {
                snprintf (errstr, 256,
                          "option %s %s: '%s' is not a valid boolean value",
                          key, value, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}


static int
xlator_option_validate_xlator (xlator_t *xl, const char *key, const char *value,
                               volume_option_t *opt, char **op_errstr)
{
        int          ret = -1;
        char         errstr[256];
        xlator_t    *xlopt = NULL;


        /* Check if the value is one of the xlators */
        xlopt = xl;
        while (xlopt->prev)
                xlopt = xlopt->prev;

        while (xlopt) {
                if (strcmp (value, xlopt->name) == 0) {
                        ret = 0;
                        break;
                }
                xlopt = xlopt->next;
        }

        if (!xlopt) {
                snprintf (errstr, 256,
                          "option %s %s: '%s' is not a valid volume name",
                          key, value, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}

void
set_error_str (char *errstr, size_t len, volume_option_t *opt, const char *key,
               const char *value)
{
        int i   = 0;
        int ret = 0;

        ret = snprintf (errstr, len, "option %s %s: '%s' is not valid "
                        "(possible options are ", key, value, value);

        for (i = 0; (i < ZR_OPTION_MAX_ARRAY_SIZE) && opt->value[i];) {
                ret += snprintf (errstr + ret, len - ret, "%s", opt->value[i]);
                if (((++i) < ZR_OPTION_MAX_ARRAY_SIZE) &&
                    (opt->value[i]))
                        ret += snprintf (errstr + ret, len - ret, ", ");
                else
                        ret += snprintf (errstr + ret, len - ret, ".)");
        }
        return;
}

int
is_all_whitespaces (const char *value)
{
        int i = 0;
        size_t len = 0;

        if (value == NULL)
                return -1;

        len = strlen (value);

        for (i = 0; i < len; i++) {
                if (value[i] == ' ')
                        continue;
                else
                        return 0;
        }

        return 1;
}

static int
xlator_option_validate_str (xlator_t *xl, const char *key, const char *value,
                            volume_option_t *opt, char **op_errstr)
{
        int          ret = -1;
        int          i = 0;
        char         errstr[4096] = {0,};

        /* Check if the '*str' is valid */
        if (GF_OPTION_LIST_EMPTY(opt)) {
                ret = 0;
                goto out;
        }

        if (is_all_whitespaces (value) == 1)
                goto out;

        for (i = 0; (i < ZR_OPTION_MAX_ARRAY_SIZE) && opt->value[i]; i++) {
 #ifdef  GF_DARWIN_HOST_OS
                if (fnmatch (opt->value[i], value, 0) == 0) {
                        ret = 0;
                        break;
                }
 #else
                if (fnmatch (opt->value[i], value, FNM_EXTMATCH) == 0) {
                        ret = 0;
                        break;
                }
 #endif
        }

        if ((i == ZR_OPTION_MAX_ARRAY_SIZE) || (!opt->value[i]))
                goto out;
                /* enter here only if
                 * 1. reached end of opt->value array and haven't
                 *    validated input
                 *                      OR
                 * 2. valid input list is less than
                 *    ZR_OPTION_MAX_ARRAY_SIZE and input has not
                 *    matched all possible input values.
                 */

        ret = 0;

out:
        if (ret) {
                set_error_str (errstr, sizeof (errstr), opt, key, value);

                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                if (op_errstr)
                        *op_errstr = gf_strdup (errstr);
        }
        return ret;
}


static int
xlator_option_validate_percent (xlator_t *xl, const char *key, const char *value,
                                volume_option_t *opt, char **op_errstr)
{
        double    percent = 0;
        int       ret = -1;
        char      errstr[256];

        /* Check if the value is valid percentage */
        if (gf_string2percent (value, &percent) != 0) {
                snprintf (errstr, 256,
                          "invalid percent format \"%s\" in \"option %s\"",
                          value, key);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        if ((percent < 0.0) || (percent > 100.0)) {
                snprintf (errstr, 256,
                          "'%lf' in 'option %s %s' is out of range [0 - 100]",
                          percent, key, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_OUT_OF_RANGE, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}

static int
xlator_option_validate_fractional_value (const char *value)
{
        const char *s   = NULL;
        int        ret  = 0;

        s = strchr (value, '.');
        if (s) {
                for (s = s+1; *s != '\0'; s++) {
                        if (*s != '0') {
                                return -1;
                        }
                }
        }

        return ret;
}

static int
xlator_option_validate_percent_or_sizet (xlator_t *xl, const char *key,
                                         const char *value,
                                         volume_option_t *opt, char **op_errstr)
{
        int               ret = -1;
        char              errstr[256];
        double            size = 0;
	gf_boolean_t is_percent = _gf_false;

	if (gf_string2percent_or_bytesize (value, &size, &is_percent) == 0) {
		if (is_percent) {
                        if ((size < 0.0) || (size > 100.0)) {
                                snprintf (errstr, sizeof (errstr),
                                          "'%lf' in 'option %s %s' is out"
                                          " of range [0 - 100]", size, key,
                                          value);
                                gf_msg (xl->name, GF_LOG_ERROR, 0,
                                        LG_MSG_OUT_OF_RANGE, "%s", errstr);
                                goto out;
                        }
			ret = 0;
			goto out;
		}

                /*Input value of size(in byte) should not be fractional*/
                ret = xlator_option_validate_fractional_value (value);
                if (ret) {
                        snprintf (errstr, sizeof (errstr), "'%lf' in 'option %s"
                                  " %s' should not be fractional value. Use "
                                  "valid unsigned integer value.", size, key,
                                  value);
                        gf_msg (xl->name, GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_ENTRY, "%s", errstr);
                        goto out;
                }

		/* Check the range */
		if ((opt->min == 0) && (opt->max == 0)) {
			gf_msg_trace (xl->name, 0, "no range check required "
                                      "for 'option %s %s'", key, value);
			ret = 0;
			goto out;
		}
		if ((size < opt->min) || (size > opt->max)) {
			snprintf (errstr, 256,
				  "'%lf' in 'option %s %s'"
				  " is out of range [%.0f - %.0f]",
				  size, key, value, opt->min, opt->max);
			gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_OUT_OF_RANGE,
                                "%s", errstr);
			goto out;
		}
		ret = 0;
		goto out;
	}

	/* If control reaches here, invalid argument */

	snprintf (errstr, 256,
		  "invalid number format \"%s\" in \"option %s\"",
		  value, key);
	gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s", errstr);


out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}


static int
xlator_option_validate_time (xlator_t *xl, const char *key, const char *value,
                             volume_option_t *opt, char **op_errstr)
{
        int          ret = -1;
        char         errstr[256];
        uint32_t     input_time = 0;

	/* Check if the value is valid time */
        if (gf_string2time (value, &input_time) != 0) {
                snprintf (errstr, 256,
                          "invalid time format \"%s\" in "
                          "\"option %s\"",
                          value, key);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        if ((opt->min == 0) && (opt->max == 0)) {
                gf_msg_trace (xl->name, 0, "no range check required for "
                              "'option %s %s'", key, value);
                ret = 0;
                goto out;
        }

        if ((input_time < opt->min) || (input_time > opt->max)) {
                snprintf (errstr, 256,
                          "'%"PRIu32"' in 'option %s %s' is "
                          "out of range [%.0f - %.0f]",
                          input_time, key, value,
                          opt->min, opt->max);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_OUT_OF_RANGE, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}


static int
xlator_option_validate_double (xlator_t *xl, const char *key, const char *value,
                               volume_option_t *opt, char **op_errstr)
{
        double    input = 0.0;
        int       ret = -1;
        char      errstr[256];

        /* Check the range */
        if (gf_string2double (value, &input) != 0) {
                snprintf (errstr, 256,
                          "invalid number format \"%s\" in option \"%s\"",
                          value, key);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                goto out;
        }

        if ((opt->min == 0) && (opt->max == 0) &&
            (opt->validate == GF_OPT_VALIDATE_BOTH)) {
                gf_msg_trace (xl->name, 0, "no range check required for "
                              "'option %s %s'", key, value);
                ret = 0;
                goto out;
        }

        if (opt->validate == GF_OPT_VALIDATE_MIN) {
                if (input < opt->min) {
                        snprintf (errstr, 256,
                                  "'%f' in 'option %s %s' is smaller than "
                                  "minimum value '%f'", input, key,
                                  value, opt->min);
                        gf_msg (xl->name, GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_ENTRY, "%s", errstr);
                        goto out;
                }
        } else if (opt->validate == GF_OPT_VALIDATE_MAX) {
                if (input > opt->max) {
                        snprintf (errstr, 256,
                                  "'%f' in 'option %s %s' is greater than "
                                  "maximum value '%f'", input, key,
                                  value, opt->max);
                        gf_msg (xl->name, GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_ENTRY, "%s", errstr);
                        goto out;
                }
        } else if ((input < opt->min) || (input > opt->max)) {
                snprintf (errstr, 256,
                          "'%f' in 'option %s %s' is out of range "
                          "[%f - %f]",
                          input, key, value, opt->min, opt->max);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_OUT_OF_RANGE, "%s",
                        errstr);
                goto out;
        }

        ret = 0;
out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup (errstr);
        return ret;
}


static int
xlator_option_validate_addr (xlator_t *xl, const char *key, const char *value,
                             volume_option_t *opt, char **op_errstr)
{
        int          ret = -1;
        char         errstr[256];

        if (!valid_internet_address ((char *)value, _gf_false)) {
                snprintf (errstr, 256,
                          "option %s %s: '%s'  is not a valid internet-address,"
                          " it does not conform to standards.",
                          key, value, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                if (op_errstr)
                        *op_errstr = gf_strdup (errstr);
        }

        ret = 0;

        return ret;
}

static int
xlator_option_validate_addr_list (xlator_t *xl, const char *key,
                                  const char *value, volume_option_t *opt,
                                  char **op_errstr)
{
        int          ret = -1;
        char         *dup_val = NULL;
        char         *addr_tok = NULL;
        char         *save_ptr = NULL;
        char         errstr[4096] = {0,};

        dup_val = gf_strdup (value);
        if (!dup_val)
                goto out;

        addr_tok = strtok_r (dup_val, ",", &save_ptr);
        if (addr_tok == NULL)
                goto out;
        while (addr_tok) {
                if (!valid_internet_address (addr_tok, _gf_true))
                        goto out;

                addr_tok = strtok_r (NULL, ",", &save_ptr);
        }
        ret = 0;

out:
        if (ret) {
                snprintf (errstr, sizeof (errstr), "option %s %s: '%s' is not "
                "a valid internet-address-list", key, value, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                if (op_errstr)
                        *op_errstr = gf_strdup (errstr);
        }
        GF_FREE (dup_val);

        return ret;
}

static int
xlator_option_validate_mntauth (xlator_t *xl, const char *key,
                                const char *value, volume_option_t *opt,
                                char **op_errstr)
{
        int          ret = -1;
        char         *dup_val = NULL;
        char         *addr_tok = NULL;
        char         *save_ptr = NULL;
        char         errstr[4096] = {0,};

        dup_val = gf_strdup (value);
        if (!dup_val)
                goto out;

        addr_tok = strtok_r (dup_val, ",", &save_ptr);
        if (addr_tok == NULL)
                goto out;
        while (addr_tok) {
                if (!valid_mount_auth_address (addr_tok))
                        goto out;

                addr_tok = strtok_r (NULL, ",", &save_ptr);
        }
        ret = 0;

out:
        if (ret) {
                snprintf (errstr, sizeof (errstr), "option %s %s: '%s' is not "
                "a valid mount-auth-address", key, value, value);
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY, "%s",
                        errstr);
                if (op_errstr)
                        *op_errstr = gf_strdup (errstr);
        }
        GF_FREE (dup_val);

        return ret;
}

/*XXX: the rules to validate are as per block-size required for stripe xlator */
static int
gf_validate_size (const char *sizestr, volume_option_t *opt)
{
        size_t                value = 0;
        int                     ret = 0;

        GF_ASSERT (opt);

        if (gf_string2bytesize_size (sizestr, &value) != 0 ||
            value < opt->min ||
            value % 512) {
                ret = -1;
                goto out;
        }

 out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

static int
gf_validate_number (const char *numstr, volume_option_t *opt)
{
        int32_t value;
        return gf_string2int32 (numstr, &value);
}

/*  Parses the string to be of the form <key1>:<value1>,<key2>:<value2>...  *
 *  takes two optional validaters key_validator and value_validator         */
static int
validate_list_elements (const char *string, volume_option_t *opt,
                        int (key_validator)( const char *),
                        int (value_validator)( const char *, volume_option_t *))
{

        char                    *dup_string = NULL;
        char                    *str_sav = NULL;
        char                    *substr_sav = NULL;
        char                    *str_ptr = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        int                     ret = 0;

        GF_ASSERT (string);

        dup_string = gf_strdup (string);
        if (NULL == dup_string)
                goto out;

        str_ptr = strtok_r (dup_string, ",", &str_sav);
        if (str_ptr == NULL) {
                ret = -1;
                goto out;
        }
        while (str_ptr) {

                key = strtok_r (str_ptr, ":", &substr_sav);
                if (!key ||
                    (key_validator && key_validator(key))) {
                        ret = -1;
                        gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                LG_MSG_INVALID_ENTRY, "invalid list '%s', key "
                                "'%s' not valid.", string, key);
                        goto out;
                }

                value = strtok_r (NULL, ":", &substr_sav);
                if (!value ||
                    (value_validator && value_validator(value, opt))) {
                        ret = -1;
                        gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                LG_MSG_INVALID_ENTRY, "invalid list '%s', "
                                "value '%s' not valid.", string, key);
                        goto out;
                }

                str_ptr = strtok_r (NULL, ",", &str_sav);
                substr_sav = NULL;
        }

 out:
        GF_FREE (dup_string);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

static int
xlator_option_validate_priority_list (xlator_t *xl, const char *key,
                                      const char *value, volume_option_t *opt,
                                      char **op_errstr)
{
        int                     ret =0;
        char                    errstr[1024] = {0, };

        GF_ASSERT (value);

        ret = validate_list_elements (value, opt, NULL, &gf_validate_number);
        if (ret) {
                snprintf (errstr, 1024,
                          "option %s %s: '%s' is not a valid "
                          "priority-list", key, value, value);
                *op_errstr = gf_strdup (errstr);
        }

        return ret;
}

static int
xlator_option_validate_size_list (xlator_t *xl, const char *key,
                                  const char *value, volume_option_t *opt,
                                  char **op_errstr)
{

        int                    ret = 0;
        char                   errstr[1024] = {0, };

        GF_ASSERT (value);

        ret = gf_validate_size (value, opt);
        if (ret)
                ret = validate_list_elements (value, opt, NULL, &gf_validate_size);

        if (ret) {
                snprintf (errstr, 1024,
                          "option %s %s: '%s' is not a valid "
                          "size-list", key, value, value);
                *op_errstr = gf_strdup (errstr);
        }

        return ret;

}

static int
xlator_option_validate_any (xlator_t *xl, const char *key, const char *value,
                            volume_option_t *opt, char **op_errstr)
{
        return 0;
}

typedef int (xlator_option_validator_t) (xlator_t *xl, const char *key,
                                         const char *value,
                                         volume_option_t *opt, char **operrstr);

int
xlator_option_validate (xlator_t *xl, char *key, char *value,
                        volume_option_t *opt, char **op_errstr)
{
        int       ret = -1;
        xlator_option_validator_t *validate;
        xlator_option_validator_t *validators[] = {
                [GF_OPTION_TYPE_PATH]        = xlator_option_validate_path,
                [GF_OPTION_TYPE_INT]         = xlator_option_validate_int,
                [GF_OPTION_TYPE_SIZET]       = xlator_option_validate_sizet,
                [GF_OPTION_TYPE_BOOL]        = xlator_option_validate_bool,
                [GF_OPTION_TYPE_XLATOR]      = xlator_option_validate_xlator,
                [GF_OPTION_TYPE_STR]         = xlator_option_validate_str,
                [GF_OPTION_TYPE_PERCENT]     = xlator_option_validate_percent,
                [GF_OPTION_TYPE_PERCENT_OR_SIZET] =
                xlator_option_validate_percent_or_sizet,
                [GF_OPTION_TYPE_TIME]        = xlator_option_validate_time,
                [GF_OPTION_TYPE_DOUBLE]      = xlator_option_validate_double,
                [GF_OPTION_TYPE_INTERNET_ADDRESS] = xlator_option_validate_addr,
                [GF_OPTION_TYPE_INTERNET_ADDRESS_LIST] =
                xlator_option_validate_addr_list,
                [GF_OPTION_TYPE_PRIORITY_LIST] =
                xlator_option_validate_priority_list,
                [GF_OPTION_TYPE_SIZE_LIST]   = xlator_option_validate_size_list,
                [GF_OPTION_TYPE_ANY]         = xlator_option_validate_any,
                [GF_OPTION_TYPE_CLIENT_AUTH_ADDR] = xlator_option_validate_mntauth,
                [GF_OPTION_TYPE_MAX]         = NULL,
        };

        if (opt->type > GF_OPTION_TYPE_MAX) {
                gf_msg (xl->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "unknown option type '%d'", opt->type);
                goto out;
        }

        validate = validators[opt->type];

        ret = validate (xl, key, value, opt, op_errstr);
out:
        return ret;
}


volume_option_t *
xlator_volume_option_get_list (volume_opt_list_t *vol_list, const char *key)
{
        volume_option_t         *opt = NULL;
        volume_opt_list_t       *opt_list = NULL;
        volume_option_t         *found = NULL;
        int                      index = 0;
        int                      i = 0;
        char                    *cmp_key = NULL;

        if (!vol_list->given_opt) {
                opt_list = list_entry (vol_list->list.next, volume_opt_list_t,
                                       list);
                opt = opt_list->given_opt;
        } else
                opt = vol_list->given_opt;

        for (index = 0; opt[index].key[0]; index++) {
                for (i = 0; i < ZR_VOLUME_MAX_NUM_KEY; i++) {
                        cmp_key = opt[index].key[i];
                        if (!cmp_key)
                                break;
                        if (fnmatch (cmp_key, key, FNM_NOESCAPE) == 0) {
                                found = &opt[index];
                                goto out;
                        }
                }
        }
out:
        return found;
}


volume_option_t *
xlator_volume_option_get (xlator_t *xl, const char *key)
{
        volume_opt_list_t       *vol_list = NULL;
        volume_option_t         *found = NULL;

        list_for_each_entry (vol_list, &xl->volume_options, list) {
                found = xlator_volume_option_get_list (vol_list, key);
                if (found)
                        break;
        }

        return found;
}


static int
xl_opt_validate (dict_t *dict, char *key, data_t *value, void *data)
{
        xlator_t          *xl = NULL;
        volume_opt_list_t *vol_opt = NULL;
        volume_option_t   *opt = NULL;
        int                ret = 0;
        char              *errstr = NULL;

        struct {
                xlator_t           *this;
                volume_opt_list_t  *vol_opt;
                char               *errstr;
        } *stub;

        stub = data;
        xl = stub->this;
        vol_opt = stub->vol_opt;

        opt = xlator_volume_option_get_list (vol_opt, key);
        if (!opt)
                return 0;

        ret = xlator_option_validate (xl, key, value->data, opt, &errstr);
        if (ret)
                gf_msg (xl->name, GF_LOG_WARNING, 0, LG_MSG_VALIDATE_RETURNS,
                        "validate of %s returned %d", key, ret);

        if (errstr)
                /* possible small leak of previously set stub->errstr */
                stub->errstr = errstr;

        if (fnmatch (opt->key[0], key, FNM_NOESCAPE) != 0) {
                gf_msg (xl->name, GF_LOG_WARNING, 0, LG_MSG_INVALID_ENTRY,
                        "option '%s' is deprecated, preferred is '%s', "
                        "continuing with correction", key, opt->key[0]);
                dict_set (dict, opt->key[0], value);
                dict_del (dict, key);
        }
        return 0;
}


int
xlator_options_validate_list (xlator_t *xl, dict_t *options,
                              volume_opt_list_t *vol_opt, char **op_errstr)
{
        int ret = 0;
        struct {
                xlator_t           *this;
                volume_opt_list_t  *vol_opt;
                char               *errstr;
        } stub;

        stub.this = xl;
        stub.vol_opt = vol_opt;
        stub.errstr = NULL;

        dict_foreach (options, xl_opt_validate, &stub);
        if (stub.errstr) {
                ret = -1;
                if (op_errstr)
                        *op_errstr = stub.errstr;
        }

        return ret;
}


int
xlator_options_validate (xlator_t *xl, dict_t *options, char **op_errstr)
{
        int                ret     = 0;
        volume_opt_list_t *vol_opt = NULL;


        if (!xl) {
                gf_msg_debug (THIS->name, 0, "'this' not a valid ptr");
                ret = -1;
                goto out;
        }

        if (list_empty (&xl->volume_options))
                goto out;

        list_for_each_entry (vol_opt, &xl->volume_options, list) {
                ret = xlator_options_validate_list (xl, options, vol_opt,
                                                    op_errstr);
        }
out:
        return ret;
}


int
xlator_validate_rec (xlator_t *xlator, char **op_errstr)
{
        int            ret  = -1;
        xlator_list_t *trav = NULL;
        xlator_t      *old_THIS = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", xlator, out);

        trav = xlator->children;

        while (trav) {
                if (xlator_validate_rec (trav->xlator, op_errstr)) {
                        gf_msg ("xlator", GF_LOG_WARNING, 0,
                                LG_MSG_VALIDATE_REC_FAILED, "validate_rec "
                                "failed");
                        goto out;
                }

                trav = trav->next;
        }

        if (xlator_dynload (xlator))
                gf_msg_debug (xlator->name, 0, "Did not load the symbols");

        old_THIS = THIS;
        THIS = xlator;

        /* Need this here, as this graph has not yet called init() */
        if (!xlator->mem_acct) {
                if (!xlator->mem_acct_init)
                        xlator->mem_acct_init = default_mem_acct_init;
                xlator->mem_acct_init (xlator);
        }

        ret = xlator_options_validate (xlator, xlator->options, op_errstr);
        THIS = old_THIS;

        if (ret) {
                gf_msg (xlator->name, GF_LOG_INFO, 0, LG_MSG_INVALID_ENTRY,
                        "%s", *op_errstr);
                goto out;
        }

        gf_msg_debug (xlator->name, 0, "Validated options");

        ret = 0;
out:
        return ret;
}


int
graph_reconf_validateopt (glusterfs_graph_t *graph, char **op_errstr)
{
        xlator_t *xlator = NULL;
        int       ret = -1;

        GF_ASSERT (graph);

        xlator = graph->first;

        ret = xlator_validate_rec (xlator, op_errstr);

        return ret;
}


static int
xlator_reconfigure_rec (xlator_t *old_xl, xlator_t *new_xl)
{
        xlator_list_t *trav1    = NULL;
        xlator_list_t *trav2    = NULL;
        int32_t        ret      = -1;
        xlator_t      *old_THIS = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", old_xl, out);
        GF_VALIDATE_OR_GOTO ("xlator", new_xl, out);

        trav1 = old_xl->children;
        trav2 = new_xl->children;

        while (trav1 && trav2) {
                ret = xlator_reconfigure_rec (trav1->xlator, trav2->xlator);
                if (ret)
                        goto out;

                gf_msg_debug (trav1->xlator->name, 0, "reconfigured");

                trav1 = trav1->next;
                trav2 = trav2->next;
        }

        if (old_xl->reconfigure) {
                old_THIS = THIS;
                THIS = old_xl;

                xlator_init_lock ();
                ret = old_xl->reconfigure (old_xl, new_xl->options);
                xlator_init_unlock ();

                THIS = old_THIS;

                if (ret)
                        goto out;
        } else {
                gf_msg_debug (old_xl->name, 0, "No reconfigure() found");
        }

        ret = 0;
out:
        return ret;
}


int
xlator_tree_reconfigure (xlator_t *old_xl, xlator_t *new_xl)
{
        xlator_t *new_top = NULL;
        xlator_t *old_top = NULL;

        GF_ASSERT (old_xl);
        GF_ASSERT (new_xl);

        old_top = old_xl;
        new_top = new_xl;

        return xlator_reconfigure_rec (old_top, new_top);
}


int
xlator_option_info_list (volume_opt_list_t *list, char *key,
                         char **def_val, char **descr)
{
        int                     ret = -1;
        volume_option_t         *opt = NULL;


        opt = xlator_volume_option_get_list (list, key);
        if (!opt)
                goto out;

        if (def_val)
                *def_val = opt->default_value;
        if (descr)
                *descr = opt->description;

        ret = 0;
out:
        return ret;
}


static int
pass (char *in, char **out)
{
        *out = in;
        return 0;
}


static int
xl_by_name (char *in, xlator_t **out)
{
        xlator_t  *xl = NULL;

        xl = xlator_search_by_name (THIS, in);

        if (!xl)
                return -1;
        *out = xl;
        return 0;
}


static int
pc_or_size (char *in, double *out)
{
        double  pc = 0;
        int       ret = 0;
        size_t  size = 0;

        if (gf_string2percent (in, &pc) == 0) {
                if (pc > 100.0) {
                        ret = gf_string2bytesize_size (in, &size);
                        if (!ret)
                                *out = size;
                } else {
                        *out = pc;
                }
        } else {
                ret = gf_string2bytesize_size (in, &size);
                if (!ret)
                        *out = size;
        }
        return ret;
}

DEFINE_INIT_OPT(char *, str, pass);
DEFINE_INIT_OPT(uint64_t, uint64, gf_string2uint64);
DEFINE_INIT_OPT(int64_t, int64, gf_string2int64);
DEFINE_INIT_OPT(uint32_t, uint32, gf_string2uint32);
DEFINE_INIT_OPT(int32_t, int32, gf_string2int32);
DEFINE_INIT_OPT(size_t, size, gf_string2bytesize_size);
DEFINE_INIT_OPT(uint64_t, size_uint64, gf_string2bytesize_uint64);
DEFINE_INIT_OPT(double, percent, gf_string2percent);
DEFINE_INIT_OPT(double, percent_or_size, pc_or_size);
DEFINE_INIT_OPT(gf_boolean_t, bool, gf_string2boolean);
DEFINE_INIT_OPT(xlator_t *, xlator, xl_by_name);
DEFINE_INIT_OPT(char *, path, pass);
DEFINE_INIT_OPT(double, double, gf_string2double);
DEFINE_INIT_OPT(uint32_t, time, gf_string2time);


DEFINE_RECONF_OPT(char *, str, pass);
DEFINE_RECONF_OPT(uint64_t, uint64, gf_string2uint64);
DEFINE_RECONF_OPT(int64_t, int64, gf_string2int64);
DEFINE_RECONF_OPT(uint32_t, uint32, gf_string2uint32);
DEFINE_RECONF_OPT(int32_t, int32, gf_string2int32);
DEFINE_RECONF_OPT(size_t, size, gf_string2bytesize_size);
DEFINE_RECONF_OPT(uint64_t, size_uint64, gf_string2bytesize_uint64);
DEFINE_RECONF_OPT(double, percent, gf_string2percent);
DEFINE_RECONF_OPT(double, percent_or_size, pc_or_size);
DEFINE_RECONF_OPT(gf_boolean_t, bool, gf_string2boolean);
DEFINE_RECONF_OPT(xlator_t *, xlator, xl_by_name);
DEFINE_RECONF_OPT(char *, path, pass);
DEFINE_RECONF_OPT(double, double, gf_string2double);
DEFINE_RECONF_OPT(uint32_t, time, gf_string2time);
