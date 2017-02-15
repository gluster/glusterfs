/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

%token VOLUME_BEGIN VOLUME_END OPTION NEWLINE SUBVOLUME ID WHITESPACE COMMENT TYPE STRING_TOK

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#define RELAX_POISONING

#include "xlator.h"
#include "graph-utils.h"
#include "logging.h"
#include "syscall.h"
#include "libglusterfs-messages.h"

static int new_volume (char *name);
static int volume_type (char *type);
static int volume_option (char *key, char *value);
static int volume_sub (char *sub);
static int volume_end (void);
static void sub_error (void);
static void type_error (void);
static void option_error (void);

#define YYSTYPE char *
#define GF_CMD_BUFFER_LEN (8 * GF_UNIT_KB)

int graphyyerror (const char *);
int graphyylex ();
%}


%%
VOLUMES: VOLUME | VOLUMES VOLUME;

VOLUME: VOLUME_HEADER VOLUME_DATA VOLUME_FOOTER;
VOLUME_HEADER: VOLUME_BEGIN WORD {if (new_volume ($2) == -1) { YYABORT; }};
VOLUME_FOOTER: VOLUME_END {if (volume_end () == -1) { YYABORT; }};

VOLUME_DATA: TYPE_LINE OPTIONS_LINE SUBVOLUME_LINE OPTIONS_LINE |
              TYPE_LINE SUBVOLUME_LINE OPTIONS_LINE |
              TYPE_LINE OPTIONS_LINE SUBVOLUME_LINE |
              TYPE_LINE SUBVOLUME_LINE |
              TYPE_LINE OPTIONS_LINE |
              OPTIONS_LINE SUBVOLUME_LINE OPTIONS_LINE | /* error case */
              OPTIONS_LINE;  /* error case */

TYPE_LINE: TYPE WORD {if (volume_type ($2) == -1) { YYABORT; }} | TYPE { type_error(); YYABORT; };

SUBVOLUME_LINE: SUBVOLUME WORDS | SUBVOLUME { sub_error (); YYABORT; };

OPTIONS_LINE: OPTION_LINE | OPTIONS_LINE OPTION_LINE;

OPTION_LINE: OPTION WORD WORD {if (volume_option ($2, $3) == -1) { YYABORT; }} |
	     OPTION WORD { option_error (); YYABORT; } |
	     OPTION { option_error (); YYABORT; };

WORDS: WORD {if (volume_sub ($1) == -1) {YYABORT; }} | WORDS WORD { if (volume_sub ($2) == -1) { YYABORT; }};
WORD: ID | STRING_TOK ;
%%

xlator_t *curr;
glusterfs_graph_t *construct;


static void
type_error (void)
{
        extern int graphyylineno;

        gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_VOLFILE_PARSE_ERROR,
                "Volume %s, before line %d: Please specify volume type",
                curr->name, graphyylineno);
        return;
}


static void
sub_error (void)
{
        extern int graphyylineno;

        gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_VOLFILE_PARSE_ERROR,
                "Volume %s, before line %d: Please specify subvolumes",
                curr->name, graphyylineno);
        return;
}


static void
option_error (void)
{
        extern int graphyylineno;

        gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_VOLFILE_PARSE_ERROR,
                "Volume %s, before line %d: Please specify "
                "option <key> <value>",
                curr->name, graphyylineno);
        return;
}


static int
new_volume (char *name)
{
        extern int   graphyylineno;
        xlator_t    *trav = NULL;
        int          ret = 0;

        if (!name) {
                gf_msg_debug ("parser", 0,"Invalid argument name: '%s'", name);
                ret = -1;
                goto out;
        }

        if (curr) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "new volume (%s) definition in line %d unexpected",
                        name, graphyylineno);
                ret = -1;
                goto out;
        }

        curr = (void *) GF_CALLOC (1, sizeof (*curr),
                                   gf_common_mt_xlator_t);

        if (!curr) {
                ret = -1;
                goto out;
        }

        trav = construct->first;

        while (trav) {
                if (!strcmp (name, trav->name)) {
                        gf_msg ("parser", GF_LOG_ERROR, 0,
                                LG_MSG_VOLFILE_PARSE_ERROR, "Line %d: volume "
                                "'%s' defined again", graphyylineno, name);
                        ret = -1;
                        goto out;
                }
                trav = trav->next;
        }

        curr->name = gf_strdup (name);
        if (!curr->name) {
                GF_FREE (curr);
                ret = -1;
                goto out;
        }

        curr->options = get_new_dict ();

        if (!curr->options) {
                GF_FREE (curr->name);
                GF_FREE (curr);
                ret = -1;
                goto out;
        }

        curr->next = construct->first;
        if (curr->next)
                curr->next->prev = curr;

        curr->graph = construct;

        construct->first = curr;

        construct->xl_count++;
        curr->xl_id = construct->xl_count;

        gf_msg_trace ("parser", 0, "New node for '%s'", name);

out:
        GF_FREE (name);

        return ret;
}


static int
volume_type (char *type)
{
        extern int   graphyylineno;
        int32_t      ret = 0;

        if (!type) {
                gf_msg_debug ("parser", 0, "Invalid argument type");
                ret = -1;
                goto out;
        }

        ret = xlator_set_type (curr, type);
        if (ret) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Volume '%s', line %d: type '%s' is not valid or "
			"not found on this machine",
                        curr->name, graphyylineno, type);
                ret = -1;
                goto out;
        }

        gf_msg_trace ("parser", 0, "Type:%s:%s", curr->name, type);

out:
        GF_FREE (type);

        return 0;
}


static int
volume_option (char *key, char *value)
{
        extern int  graphyylineno;
        int         ret = 0;
        char       *set_value = NULL;

        if (!key || !value){
                gf_msg ("parser", GF_LOG_ERROR, 0,
                        LG_MSG_INVALID_VOLFILE_ENTRY, "Invalid argument");
                ret = -1;
                goto out;
        }

        set_value = gf_strdup (value);
	ret = dict_set_dynstr (curr->options, key, set_value);

        if (ret == 1) {
                gf_msg ("parser", GF_LOG_ERROR, 0,
                        LG_MSG_INVALID_VOLFILE_ENTRY, "Volume '%s', line %d: "
                        "duplicate entry ('option %s') present",
                        curr->name, graphyylineno, key);
                ret = -1;
                goto out;
        }

        gf_msg_trace ("parser", 0, "Option:%s:%s:%s", curr->name, key, value);

out:
        GF_FREE (key);
        GF_FREE (value);

        return 0;
}


static int
volume_sub (char *sub)
{
        extern int       graphyylineno;
        xlator_t        *trav = NULL;
        int              ret = 0;

        if (!sub) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Invalid subvolumes argument");
                ret = -1;
                goto out;
        }

        trav = construct->first;

        while (trav) {
                if (!strcmp (sub,  trav->name))
                        break;
                trav = trav->next;
        }

        if (!trav) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_SUB_VOLUME_ERROR,
                        "Volume '%s', line %d: subvolume '%s' is not defined "
			"prior to usage",curr->name, graphyylineno, sub);
                ret = -1;
                goto out;
        }

        if (trav == curr) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Volume '%s', line %d: has '%s' itself as subvolume",
                        curr->name, graphyylineno, sub);
                ret = -1;
                goto out;
        }

	ret = glusterfs_xlator_link (curr, trav);
	if (ret) {
                ret = -1;
                goto out;
        }

        gf_msg_trace ("parser", 0, "child:%s->%s", curr->name, sub);

out:
        GF_FREE (sub);

        return 0;
}


static int
volume_end (void)
{
        if (!curr->fops) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_VOLUME_ERROR,
                        "\"type\" not specified for volume %s", curr->name);
                return -1;
        }
        gf_msg_trace ("parser", 0, "end:%s", curr->name);

        curr = NULL;
        return 0;
}


int
graphyywrap ()
{
        return 1;
}


int
graphyyerror (const char *str)
{
        extern char  *graphyytext;
        extern int    graphyylineno;

        if (curr && curr->name && graphyytext) {
                if (!strcmp (graphyytext, "volume")) {
                        gf_msg ("parser", GF_LOG_ERROR, 0,
                                LG_MSG_VOLUME_ERROR, "'end-volume' not"
                                " defined for volume '%s'", curr->name);
                } else if (!strcmp (graphyytext, "type")) {
                        gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_VOLUME_ERROR,
                                "line %d: duplicate 'type' defined for "
				"volume '%s'", graphyylineno, curr->name);
                } else if (!strcmp (graphyytext, "subvolumes")) {
                        gf_msg ("parser", GF_LOG_ERROR, 0,
                                LG_MSG_SUB_VOLUME_ERROR, "line %d: duplicate "
                                "'subvolumes' defined for volume '%s'",
                                graphyylineno, curr->name);
                } else if (curr) {
                        gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_SYNTAX_ERROR,
                                "syntax error: line %d (volume '%s'): \"%s\""
				"\nallowed tokens are 'volume', 'type', "
				"'subvolumes', 'option', 'end-volume'()",
                                graphyylineno, curr->name, graphyytext);
                } else {
                        gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_SYNTAX_ERROR,
                                "syntax error: line %d (just after volume "
				"'%s'): \"%s\"\n(%s)",
                                graphyylineno, curr->name, graphyytext,
                                "allowed tokens are 'volume', 'type', "
				"'subvolumes', 'option', 'end-volume'");
                }
        } else {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_SYNTAX_ERROR,
                        "syntax error in line %d: \"%s\"\n"
                        "(allowed tokens are 'volume', 'type', "
			"'subvolumes', 'option', 'end-volume')\n",
                        graphyylineno, graphyytext);
        }

        return -1;
}


static int
execute_cmd (char *cmd, char **result, size_t size)
{
	FILE       *fpp = NULL;
	int         i = 0;
        int         status = 0;
	int         character = 0;
	char       *buf = *result;

	fpp = popen (cmd, "r");
	if (!fpp) {
		gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_FILE_OP_FAILED,
                        "%s: failed to popen", cmd);
		return -1;
	}

	while ((character = fgetc (fpp)) != EOF) {
		if (i == size) {
			size *= 2;
			buf = *result = GF_REALLOC (*result, size);
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
preprocess (FILE *srcfp, FILE *dstfp)
{
	int     ret = 0;
        int     i = 0;
	char   *cmd = NULL;
        char   *result = NULL;
	size_t  cmd_buf_size = GF_CMD_BUFFER_LEN;
	char    escaped = 0;
        char    in_backtick = 0;
	int     line = 1;
        int     column = 0;
        int     character = 0;


	fseek (srcfp, 0L, SEEK_SET);
	fseek (dstfp, 0L, SEEK_SET);

	cmd = GF_CALLOC (cmd_buf_size, 1,
                         gf_common_mt_char);
        if (cmd == NULL) {
                return -1;
        }

	result = GF_CALLOC (cmd_buf_size * 2, 1,
                            gf_common_mt_char);
        if (result == NULL) {
                GF_FREE (cmd);
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
			}

			in_backtick = !in_backtick;
		} else {
			if (in_backtick) {
				if (i == cmd_buf_size) {
					cmd_buf_size *= 2;
					cmd = GF_REALLOC (cmd, cmd_buf_size);
                                        if (cmd == NULL) {
                                                GF_FREE (result);
                                                return -1;
                                        }

					result = GF_REALLOC (result,
                                                             2 * cmd_buf_size);
                                        if (result == NULL) {
                                                GF_FREE (cmd);
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
		gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_VOLUME_ERROR,
			"Unterminated backtick in volume specification file at "
                        "line (%d), column (%d).", line, column);
                ret = -1;
	}

out:
	fseek (srcfp, 0L, SEEK_SET);
	fseek (dstfp, 0L, SEEK_SET);

	GF_FREE (cmd);
	GF_FREE (result);

	return ret;
}


extern FILE *graphyyin;

glusterfs_graph_t *
glusterfs_graph_new ()
{
        glusterfs_graph_t *graph = NULL;

        graph = GF_CALLOC (1, sizeof (*graph),
                           gf_common_mt_glusterfs_graph_t);
        if (!graph)
                return NULL;

        INIT_LIST_HEAD (&graph->list);

        gettimeofday (&graph->dob, NULL);

        return graph;
}


glusterfs_graph_t *
glusterfs_graph_construct (FILE *fp)
{
        int                ret = 0;
        int                tmp_fd = -1;
        glusterfs_graph_t *graph = NULL;
        FILE              *tmp_file = NULL;
        char               template[PATH_MAX] = {0};
	static pthread_mutex_t graph_mutex = PTHREAD_MUTEX_INITIALIZER;

        graph = glusterfs_graph_new ();
        if (!graph)
                goto err;

        strcpy (template, "/tmp/tmp.XXXXXX");
        tmp_fd = mkstemp (template);
        if (-1 == tmp_fd)
                goto err;

        ret = sys_unlink (template);
        if (ret < 0) {
                gf_msg ("parser", GF_LOG_WARNING, 0, LG_MSG_FILE_OP_FAILED,
                        "Unable to delete file: %s", template);
        }

        tmp_file = fdopen (tmp_fd, "w+b");
        if (!tmp_file)
                goto err;

        ret = preprocess (fp, tmp_file);
        if (ret < 0) {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_BACKTICK_PARSE_FAILED,
                        "parsing of backticks failed");
                goto err;
        }

	pthread_mutex_lock (&graph_mutex);
	{
		graphyyin = tmp_file;
		construct = graph;
		ret = yyparse ();
		construct = NULL;
	}
	pthread_mutex_unlock (&graph_mutex);

        if (ret == 1) {
                gf_msg_debug ("parser", 0, "parsing of volfile failed, please "
                              "review it once more");
                goto err;
        }

        fclose (tmp_file);
        return graph;
err:
        if (tmp_file) {
                fclose (tmp_file);
        } else {
                gf_msg ("parser", GF_LOG_ERROR, 0, LG_MSG_FILE_OP_FAILED,
                        "cannot create temporary file");
                if (-1 != tmp_fd)
                        sys_close (tmp_fd);
        }

        glusterfs_graph_destroy (graph);
        return NULL;
}

