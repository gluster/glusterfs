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

#ifndef _PARSE_UTILS_H
#define _PARSE_UTILS_H

#include <regex.h>

#define GF_PARSE "parse-utils"

struct parser {
        regex_t preg;           /* Compiled regex */
        regmatch_t pmatch[1];   /* The match */
        char *complete_str;     /* The string we are parsing */
        char *regex;            /* Regex used to parse the string */
        char *_rstr;            /* Temp string to hold offsets */
};

/* Initializes some of the parsers variables */
struct parser *
parser_init (const char *regex);

/* Sets the string to parse */
int
parser_set_string (struct parser *parser, const char *complete_str);

/* Frees memory used by the string after all matches are found */
int
parser_unset_string (struct parser *parser);

/* Free memory used by the parser */
void
parser_deinit (struct parser *ptr);

/* Get the next matching string */
char *
parser_get_next_match (struct parser *parser);

#endif /* _PARSE_UTILS_H */
