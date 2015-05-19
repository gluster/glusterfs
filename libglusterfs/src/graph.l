/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

%x STRING
%option yylineno
%option noinput
%{

#define YYSTYPE char *
#include "xlator.h"
#include "y.tab.h"
#include <string.h>
#define START_STRSIZE	32

static char *text;
static int text_asize;
static int text_size;

void append_string(const char *str, int size)
{
	int new_size = text_size + size + 1;
	if (new_size > text_asize) {
		new_size += START_STRSIZE - 1;
		new_size &= -START_STRSIZE;
                if (!text) {
                        text = GF_CALLOC (1, new_size,
                                          gf_common_mt_char);
                } else {
                        text = GF_REALLOC (text, new_size);
                }
                if (!text) {
                        return;
                }
		text_asize = new_size;
	}
        memcpy(text + text_size, str, size);
	text_size += size;
	text[text_size] = 0;
}

%}

VOLUME [v][o][l][u][m][e]
END    [e][n][d]
SUB    [s][u][b]
OPTION [o][p][t][i][o][n]
TYPE   [t][y][p][e]
%%
\#.*                   ;
{VOLUME}               return VOLUME_BEGIN;
{TYPE}                 return TYPE;
{END}[-]{VOLUME}       return VOLUME_END;
{SUB}{VOLUME}[Ss]      return SUBVOLUME;
{OPTION}               return OPTION;
\"                     BEGIN(STRING);
<STRING>{
       [^\n\"\\]* {    append_string (yytext, yyleng); }
       \\.        {    append_string (yytext + 1, yyleng - 1); }
       \"         {
                       if (0) {
                              yyunput (0, NULL);
                       }
                       BEGIN (INITIAL);
                       graphyylval = text;
                       return STRING_TOK;
       }
}
[^ \t\r\n\"\\]+ {    graphyylval = gf_strdup (yytext) ; return ID; }
[ \t\r\n]+             ;
%%
