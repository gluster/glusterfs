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


%token SECTION_BEGIN SECTION_END OPTION NEWLINE SUBSECTION ID WHITESPACE COMMENT TYPE STRING_TOK 

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
	 
#include "xlator.h"
#include "logging.h"

static int new_section (char *name);
static int section_type (char *type);
static int section_option (char *key, char *value);
static int section_sub (char *sub);
static int section_end (void);
static void sub_error (void);
static void type_error (void);
static void option_error (void);

#define YYSTYPE char *
#define GF_CMD_BUFFER_LEN (8 * GF_UNIT_KB) 

int yyerror (const char *);
int yylex ();
%}


%%
SECTIONS: SECTION | SECTIONS SECTION;

SECTION: SECTION_HEADER SECTION_DATA SECTION_FOOTER;
SECTION_HEADER: SECTION_BEGIN WORD {if( -1 == new_section ($2)) { YYABORT; } };
SECTION_FOOTER: SECTION_END {if( -1 == section_end ()) { YYABORT; } };

SECTION_DATA: TYPE_LINE OPTIONS_LINE SUBSECTION_LINE OPTIONS_LINE |
              TYPE_LINE SUBSECTION_LINE OPTIONS_LINE |
              TYPE_LINE OPTIONS_LINE SUBSECTION_LINE |
              TYPE_LINE SUBSECTION_LINE |
              TYPE_LINE OPTIONS_LINE |
              OPTIONS_LINE SUBSECTION_LINE OPTIONS_LINE | /* error case */
              OPTIONS_LINE;  /* error case */

TYPE_LINE: TYPE WORD {if ( -1 == section_type ($2)) { YYABORT; }} | TYPE { type_error(); YYABORT; };

SUBSECTION_LINE: SUBSECTION WORDS | SUBSECTION { sub_error (); YYABORT; };

OPTIONS_LINE: OPTION_LINE | OPTIONS_LINE OPTION_LINE;

OPTION_LINE: OPTION WORD WORD {if(-1 == section_option($2,$3)){YYABORT;} } |
	     OPTION WORD { option_error (); YYABORT; } |
	     OPTION { option_error (); YYABORT; };

WORDS: WORD {if (-1 == section_sub ($1)) {YYABORT; } } | WORDS WORD { if (-1 == section_sub ($2)) { YYABORT; } };
WORD: ID | STRING_TOK ;
%%

xlator_t *complete_tree = NULL;
xlator_t *tree = NULL;
glusterfs_ctx_t *gctx;

static void
type_error (void)
{
        extern int yylineno;

        fprintf (stderr, "Volume '%s', before line %d: Please specify volume 'type'.",
                 complete_tree->name, yylineno);
        gf_log ("parser", GF_LOG_ERROR, 
                "Volume '%s', before line %d: Please specify volume 'type'.",
                complete_tree->name, yylineno);
        return;
}

static void
sub_error (void)
{
        extern int yylineno;

        fprintf (stderr, "Volume %s, before line %d: Please specify subvolumes for the volume. ",
                 complete_tree->name, yylineno);
        gf_log ("parser", GF_LOG_ERROR, 
                "Volume %s, before line %d: Please specify subvolumes for the volume. ",
                complete_tree->name, yylineno);
        return;
}

static void
option_error (void)
{
        extern int yylineno;

        fprintf (stderr, "Volume %s, before line %d: Please specify "
		 "option <key> <value> \n",
                 complete_tree->name, yylineno);
        gf_log ("parser", GF_LOG_ERROR, 
                "Volume %s, before line %d: Please specify "
                "option <key> <value>",
                complete_tree->name, yylineno);
        return;
}

static int
cut_tree (xlator_t *tree)
{
        xlator_t *trav = tree, *prev = tree;

        if (!tree) {
                gf_log ("parser", GF_LOG_DEBUG, "Translator tree not found");
                return -1;
        }

        gf_log ("parser", GF_LOG_DEBUG, "Failed to build translator graph");

        while (prev) {
                trav = prev->next;
                dict_destroy (prev->options);
                FREE (prev->name);
                FREE (prev);
                prev = trav;
        }
  
        return 0;
}


static int
new_section (char *name)
{
        extern int yylineno;
        xlator_t *trav = complete_tree;
        xlator_t *node = (void *) calloc (1, sizeof (*node));

        if (!name) {
                gf_log ("parser", GF_LOG_DEBUG, 
			"Invalid argument name: '%s'", name);
                return -1;
        }

        while (trav) {
                if (!strcmp (name,  trav->name)) {
                        fprintf (stderr, 
				 "Line %d: volume '%s' defined again\n", 
                                 yylineno, name);
                        gf_log ("parser", GF_LOG_ERROR, 
				"Line %d: volume '%s' defined again", 
                                yylineno, name);
                        return -1;
                }
                trav = trav->next;
        }

        node->ctx = gctx;
        node->name = name;
        node->next = complete_tree;
        if (complete_tree)
                complete_tree->prev = node;
        node->options = get_new_dict ();
        complete_tree = node;

        tree = node;
        gf_log ("parser", GF_LOG_TRACE, "New node for '%s'", name);

        return 0;
}

static int
section_type (char *type)
{
        extern int yylineno;
        int32_t ret = -1;
        if (!type) {
                gf_log ("parser", GF_LOG_DEBUG, "Invalid argument type");
                return -1;
        }

        ret = xlator_set_type (tree, type);
        if (ret) {
                fprintf (stderr, "Volume '%s', line %d: type '%s' is not "
			 "valid or not found on this machine\n", 
                         complete_tree->name, yylineno, type);
                gf_log ("parser", GF_LOG_ERROR, 
                        "Volume '%s', line %d: type '%s' is not valid or "
			"not found on this machine", 
                        complete_tree->name, yylineno, type);
                return -1;
        }
        gf_log ("parser", GF_LOG_TRACE, "Type:%s:%s", tree->name, type);

        return 0;
}


static int 
section_option (char *key, char *value)
{
        extern int yylineno;

        int ret = 0;

        if (!key || !value){
                fprintf (stderr, "Invalid argument\n");
                gf_log ("parser", GF_LOG_ERROR, "Invalid argument");
                return -1;
        }

	ret = dict_set (tree->options, key, str_to_data (value));

        if (ret == 1) {
                gf_log ("parser", GF_LOG_ERROR, 
                        "Volume '%s', line %d: duplicate entry "
			"('option %s') present", 
                        tree->name, yylineno, key);
                return -1;
        }
        gf_log ("parser", GF_LOG_TRACE, "Option:%s:%s:%s",
                tree->name, key, value);

        return 0;
}

static int 
section_sub (char *sub)
{
        extern int yylineno;
        xlator_t *trav = complete_tree;
        xlator_list_t *xlchild, *tmp, *xlparent;

        if (!sub) {
                fprintf (stderr, "Invalid subvolumes argument\n");
                gf_log ("parser", GF_LOG_ERROR, "Invalid subvolumes argument");
                return -1;
        }

        while (trav) {
                if (!strcmp (sub,  trav->name))
                        break;
                trav = trav->next;
        }
        if (!trav) {
                fprintf (stderr, 
                         "Volume '%s', line %d: subvolume '%s' is not "
			 "defined prior to usage\n", 
                         complete_tree->name, yylineno, sub);
                gf_log ("parser", GF_LOG_ERROR, 
                        "Volume '%s', line %d: subvolume '%s' is not defined "
			"prior to usage", 
                        complete_tree->name, yylineno, sub);
                return -1;
        }
  
        if (trav == tree) {
                fprintf (stderr, "Volume '%s', line %d: has '%s' itself as "
			 "subvolume\n", 
                         complete_tree->name, yylineno, sub);
                gf_log ("parser", GF_LOG_ERROR, 
                        "Volume '%s', line %d: has '%s' itself as subvolume", 
                        complete_tree->name, yylineno, sub);
                return -1;
        }
  
        xlparent = (void *) calloc (1, sizeof (*xlparent));
        xlparent->xlator = tree;

        tmp = trav->parents;
        if (tmp == NULL) {
                trav->parents = xlparent;
        } else {
                while (tmp->next)
                        tmp = tmp->next;
                tmp->next = xlparent;
        }

        xlchild = (void *) calloc (1, sizeof(*xlchild));
        xlchild->xlator = trav;

        tmp = tree->children;
        if (tmp == NULL) {
                tree->children = xlchild;
        } else {
                while (tmp->next)
                        tmp = tmp->next;
                tmp->next = xlchild;
        }

        gf_log ("parser", GF_LOG_TRACE, "child:%s->%s", tree->name, sub);

        return 0;
}

static int
section_end (void)
{
        if (!tree->fops || !tree->mops) {
                fprintf (stderr, 
                         "\"type\" not specified for volume %s\n", tree->name);
                gf_log ("parser", GF_LOG_ERROR, 
                        "\"type\" not specified for volume %s", tree->name);
                return -1;
        }
        gf_log ("parser", GF_LOG_TRACE, "end:%s", tree->name);

        tree = NULL;
        return 0;
}

int
yywrap ()
{
        return 1;
}

int 
yyerror (const char *str)
{
        extern char *yytext;
        extern int yylineno;

        if (complete_tree && complete_tree->name) 
        {
                if (!strcmp (yytext, "volume")) 
                {
                        fprintf (stderr, 
                                 "'end-volume' not defined for volume '%s'\n",
				 complete_tree->name);
                        gf_log ("parser", GF_LOG_ERROR, 
                                "'end-volume' not defined for volume '%s'", 
				complete_tree->name);
                } 
                else if (!strcmp (yytext, "type")) 
                {
                        fprintf (stderr, "line %d: duplicate 'type' defined "
				 "for volume '%s'", 
                                 yylineno, complete_tree->name);
                        gf_log ("parser", GF_LOG_ERROR, 
                                "line %d: duplicate 'type' defined for "
				"volume '%s'", 
                                yylineno, complete_tree->name);
                } 
                else if (!strcmp (yytext, "subvolumes")) 
                {
                        fprintf (stderr, "line %d: duplicate 'subvolumes' "
				 "defined for volume '%s'", 
                                 yylineno, complete_tree->name);
                        gf_log ("parser", GF_LOG_ERROR, 
                                "line %d: duplicate 'subvolumes' defined for "
				"volume '%s'", 
                                yylineno, complete_tree->name);
                } 
                else if (tree) 
                {
                        fprintf (stderr, 
                                 "syntax error: line %d (volume '%s'): \"%s\""
				 "\nallowed tokens are 'volume', 'type', "
				 "'subvolumes', 'option', 'end-volume'",
                                 yylineno, complete_tree->name, 
				 yytext);
                                 
                        gf_log ("parser", GF_LOG_ERROR,
                                "syntax error: line %d (volume '%s'): \"%s\""
				"\nallowed tokens are 'volume', 'type', "
				"'subvolumes', 'option', 'end-volume'()", 
                                yylineno, complete_tree->name, 
				yytext);
                } 
                else 
                {
                        fprintf (stderr, 
                                 "syntax error: line %d (just after volume "
				 "'%s'): \"%s\"\n(%s)", 
                                 yylineno, complete_tree->name, 
				 yytext,
                                 "allowed tokens are 'volume', 'type', "
				 "'subvolumes', 'option', 'end-volume'");
                        gf_log ("parser", GF_LOG_ERROR,
                                "syntax error: line %d (just after volume "
				"'%s'): \"%s\"\n(%s)", 
                                yylineno, complete_tree->name, 
				yytext,
                                "allowed tokens are 'volume', 'type', "
				"'subvolumes', 'option', 'end-volume'");
                }
        }
        else 
        {
                fprintf (stderr, 
                         "syntax error in line %d: \"%s\" \n"
                         "(allowed tokens are 'volume', 'type', "
			 "'subvolumes', 'option', 'end-volume')\n", 
                         yylineno, yytext);
                gf_log ("parser", GF_LOG_ERROR,
                        "syntax error in line %d: \"%s\" \n"
                        "(allowed tokens are 'volume', 'type', "
			"'subvolumes', 'option', 'end-volume')\n", 
                        yylineno, yytext);
        }
  
        cut_tree (tree);
        complete_tree = NULL;
        return 0;
}

static int
execute_cmd (char *cmd, char **result, size_t size)
{
	FILE *fpp = NULL;
	int i = 0, status = 0;
	int character = 0;
	char *buf = *result;

	fpp = popen (cmd, "r");
	if (!fpp) {
		gf_log ("parser", GF_LOG_ERROR, "%s: failed to popen", cmd);
		return -1;
	}

	while ((character = fgetc (fpp)) != EOF) {
		if (i == size) {
			size *= 2;
			buf = *result = realloc (*result, size);
	}

		buf[i++] = character;
	}

	if (i > 0) {
		i--;
		buf[i] = '\0';
	}
	
	status = pclose (fpp);
	if (status == -1 || !WIFEXITED (status) || 
	    ((WEXITSTATUS (status)) != 0)) {
		i = -1;
		buf[0] = '\0';
	}

	return i;
}

static int
parse_backtick (FILE *srcfp, FILE *dstfp)
{
	int ret = 0, i = 0;
	char *cmd = NULL, *result = NULL;
	size_t cmd_buf_size = GF_CMD_BUFFER_LEN;
	char escaped = 0, in_backtick = 0;
	int line = 1, column = 0, backtick_line = 0, backtick_column = 0;
        int character = 0;

	fseek (srcfp, 0L, SEEK_SET);
	fseek (dstfp, 0L, SEEK_SET);

	cmd = CALLOC (cmd_buf_size, 1);
        if (cmd == NULL) {
                return -1;
        }

	result = CALLOC (cmd_buf_size * 2, 1);
        if (result == NULL) {
                return -1;
        }

	while ((character = fgetc (srcfp)) != EOF) {
		if ((character == '`') && !escaped) {
			if (in_backtick) {
				cmd[i] = '\0';
				result[0] = '\0';

				ret = execute_cmd (cmd, &result,
                                                   2 * cmd_buf_size);
				if (ret < 0) {
					ret = -1;
					goto out;
				}
				fwrite (result, ret, 1, dstfp);
			} else {
				i = 0;
				cmd[i] = '\0';
				
				backtick_column = column;
				backtick_line = line;
			}
			
			in_backtick = !in_backtick;
		} else {
			if (in_backtick) {
				if (i == cmd_buf_size) {
					cmd_buf_size *= 2;
					cmd = realloc (cmd, cmd_buf_size);
                                        if (cmd == NULL) {
                                                return -1;
                                        }

					result = realloc (result,
                                                          2 * cmd_buf_size);
                                        if (result == NULL) {
                                                free (cmd);
                                                return -1;
                                        }
                                }
                                
				cmd[i++] = character;
                        } else {
				fputc (character, dstfp);
                        }
                }
                
		if (character == '\\') {
			escaped = !escaped;
		} else {
			escaped = 0;
                }
		
		if (character == '\n') {
			line++;
			column = 0;
		} else {
			column++;
		}
        }
        
	if (in_backtick) {
		gf_log ("parser", GF_LOG_ERROR,
			"Unterminated backtick in volume specfication file at line (%d), column (%d).", 
			line, column);
                ret = -1;
	} 
        
out:
	fseek (srcfp, 0L, SEEK_SET);
	fseek (dstfp, 0L, SEEK_SET);
	free (cmd);
	free (result);
        
	return ret;
}

extern FILE *yyin;
xlator_t *
file_to_xlator_tree (glusterfs_ctx_t *ctx,
                     FILE *fp)
{
        int32_t ret = 0;
        xlator_t *tmp_tree = NULL;
	FILE *tmp_file = NULL;
	char *buffer = NULL;

	tmp_file = tmpfile ();
	if (NULL == tmp_file) {
		gf_log ("parser", GF_LOG_ERROR,
			"cannot create temparory file");
		return NULL;
	}

	ret = parse_backtick (fp, tmp_file);
	if (ret < 0) {
		gf_log ("parser", GF_LOG_ERROR,
			"parsing of backticks failed");
		fclose (tmp_file);
		FREE (buffer);
		return NULL;
	}

        gctx = ctx;
        yyin = tmp_file;
        ret = yyparse ();
  
	fclose (tmp_file);
	FREE (buffer);

        if (1 == ret) {
                gf_log ("parser", GF_LOG_DEBUG, 
			"parsing of volfile failed, please review it "
			"once more");
                tree = complete_tree = NULL;
                return NULL;
        }

        tmp_tree = complete_tree;
        tree = complete_tree = NULL;

        return tmp_tree;
}
