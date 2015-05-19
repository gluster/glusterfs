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

#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "parse-utils.h"
#include "mem-pool.h"
#include "common-utils.h"
#include "libglusterfs-messages.h"

/**
 * parser_init: Initialize a parser with the a string to parse and
 * the regex we want to use to parse it.
 *
 * @complete_str: the string to parse
 * @regex       : the regex to use
 *
 * Notes        : It is up to the caller to call the parser_deinit () function
 *                to free the allocated parser.
 *
 * @return      : success: parser ptr (on successful compilation and allocation)
 *              : failure: NULL (on failure to compile regex or allocate memory)
 */
struct parser *
parser_init (const char *regex)
{
        int           rc      = 0;
        struct parser *parser = NULL;

        parser = GF_MALLOC (sizeof(*parser), gf_common_mt_parser_t);
        if (!parser)
                goto out;

        parser->regex = gf_strdup (regex);
        if (!parser->regex) {
                GF_FREE (parser);
                parser = NULL;
                goto out;
        }

        rc = regcomp (&parser->preg, parser->regex, REG_EXTENDED);
        if (rc != 0) {
                gf_msg (GF_PARSE, GF_LOG_INFO, 0, LG_MSG_REGEX_OP_FAILED,
                        "Failed to compile regex pattern.");
                parser_deinit (parser);
                parser = NULL;
                goto out;
        }
        parser->complete_str = NULL;
out:
        return parser;
}

/**
 * parser_set_string -- Set the string in the parser that we want to parse.
 *                      Subsequent calls to get_next_match () will use this
 *                      string along with the regex that the parser was
 *                      initialized with.
 *
 * @parser      : The parser to use
 * @complete_str: The string to set in the parser (what we are going parse)
 *
 * @return: success: 0
 *          failure: -EINVAL for NULL args, -ENOMEM for allocation errors
 */
int
parser_set_string (struct parser *parser, const char *complete_str)
{
        int ret = -EINVAL;

        GF_VALIDATE_OR_GOTO (GF_PARSE, parser, out);
        GF_VALIDATE_OR_GOTO (GF_PARSE, complete_str, out);

        parser->complete_str = gf_strdup (complete_str);
        GF_CHECK_ALLOC_AND_LOG (GF_PARSE, parser, ret,
                                "Failed to duplicate string!", out);

        /* Point the temp internal string to what we just dup'ed */
        parser->_rstr = (char *)parser->complete_str;
        ret = 0;
out:
        return ret;
}

/**
 * parser_unset_string -- Free the string that was set to be parsed.
 *                        This function needs to be called after
 *                        parser_set_string and parser_get_next_match
 *                        in order to free memory used by the string.
 *
 * @parser      : The parser to free memory in
 * @return      : success: 0
 *              : failure: -EINVAL on NULL args
 */
int
parser_unset_string (struct parser *parser)
{
        int ret = -EINVAL;

        GF_VALIDATE_OR_GOTO (GF_PARSE, parser, out);

        GF_FREE (parser->complete_str);
        parser->complete_str = NULL; /* Avoid double frees in parser_deinit */
        ret = 0;
out:
        return ret;
}

/**
 * parser_deinit: Free the parser and all the memory allocated by it
 *
 * @parser    : Parser to free
 *
 * @return    : nothing
 */
void
parser_deinit (struct parser *ptr)
{
        if (!ptr)
                return;

        regfree (&ptr->preg);
        GF_FREE (ptr->complete_str);
        GF_FREE (ptr->regex);
        GF_FREE (ptr);
}

/**
 * parser_get_match: Given the parser that is configured with a compiled regex,
 * return the next match in the string.
 *
 * @parser    : Parser to use
 *
 * @return    : success: Pointer to matched character
 *            : failure: NULL
 */
char *
parser_get_next_match (struct parser *parser)
{
        int             rc       = -EINVAL;
        size_t          copy_len = 0;
        char            *match   = NULL;

        GF_VALIDATE_OR_GOTO (GF_PARSE, parser, out);

        rc = regexec (&parser->preg, parser->_rstr, 1, parser->pmatch, 0);
        if (rc != 0) {
                gf_msg_debug (GF_PARSE, 0,
                        "Could not match %s with regex %s",
                        parser->_rstr, parser->regex);
                goto out;
        }

        copy_len = parser->pmatch[0].rm_eo - parser->pmatch[0].rm_so;

        match = gf_strndup (parser->_rstr + parser->pmatch[0].rm_so, copy_len);
        GF_CHECK_ALLOC_AND_LOG (GF_PARSE, match, rc,
                                "Duplicating match failed!", out);

        parser->_rstr = &parser->_rstr[parser->pmatch[0].rm_eo];
out:
        return match;
}
