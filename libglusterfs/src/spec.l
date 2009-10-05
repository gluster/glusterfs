/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

void new_string(void)
{
	text = malloc(START_STRSIZE);
	text_asize = START_STRSIZE;
	text_size = 0;
	*text = 0;
}

void append_string(const char *str, int size)
{
	int new_size = text_size + size + 1;
	if (new_size > text_asize) {
		new_size += START_STRSIZE - 1;
		new_size &= -START_STRSIZE;
		text = realloc(text, new_size);
		text_asize = new_size;
	}
	memcpy(text + text_size, str, size);
	text_size += size;
	text[text_size] = 0;
}

void alloc_string(const char *str, int size)
{
	text = malloc(size + 1);
	memcpy(text, str, size);
	text[size] = 0;
}

%}

VOLUME [v][o][l][u][m][e]
END    [e][n][d]
SUB    [s][u][b]
OPTION [o][p][t][i][o][n]
TYPE   [t][y][p][e]
%%
\#.*                   ;
{VOLUME}               return SECTION_BEGIN;
{TYPE}                 return TYPE;
{END}[-]{VOLUME}       return SECTION_END;
{SUB}{VOLUME}[Ss]      return SUBSECTION;
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
                       yylval = text;
                       return STRING_TOK;
       }
}
[^ \t\r\n\"\\]+ {    yylval = strdup (yytext) ; return ID; }
[ \t\r\n]+             ;
%%
