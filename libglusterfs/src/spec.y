/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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


%token SECTION_BEGIN SECTION_END OPTION NEWLINE SUBSECTION ID WHITESPACE COMMENT TYPE STRING_TOK CMD_TOK

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "xlator.h"
#include "logging.h"

static int new_section (char *name);
static int section_type (char *type);
static int section_option (char *key, char *value);
static int section_option_cmd (char *key, char *cmd);
static int section_sub (char *sub);
static int section_end (void);

#define YYSTYPE char *
int yyerror (const char *);
int yylex ();
%}


%%
SECTIONS: SECTION | SECTIONS SECTION;

SECTION: SECTION_HEADER SECTION_DATA SECTION_FOOTER;
SECTION_HEADER: SECTION_BEGIN WORD {new_section ($2);};
SECTION_FOOTER: SECTION_END {if( -1 == section_end ()) { YYABORT; } };

SECTION_DATA:  SECTION_LINE | SECTION_DATA SECTION_LINE;
SECTION_LINE: OPTION_LINE | TYPE_LINE | SUBSECTION_LINE;

TYPE_LINE: TYPE WORD {if ( -1 == section_type ($2)) { YYABORT; }};

OPTION_LINE: OPTION WORD WORD {if(-1 == section_option($2,$3)){YYABORT;} } |
             OPTION WORD CMD {if(-1 == section_option_cmd ($2,$3)){YYABORT;} };

SUBSECTION_LINE: SUBSECTION WORDS;

WORDS: WORD {if (-1 == section_sub ($1)) {YYABORT; } } | WORDS WORD { if (-1 == section_sub ($2)) { YYABORT; } };
WORD: ID | STRING_TOK ;
CMD: CMD_TOK;
%%

xlator_t *complete_tree = NULL;
xlator_t *tree = NULL;
glusterfs_ctx_t *gctx;

static int
cut_tree (xlator_t *tree)
{
  xlator_t *trav = tree, *prev = tree;

  if (!tree) {
    gf_log ("parser", GF_LOG_ERROR, "Translator tree not found");
    return -1;
  }

  gf_log ("parser", GF_LOG_ERROR, "Failed to build translator graph");

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
  xlator_t *node = (void *) calloc (1, sizeof (*node));

  if (!name) {
    gf_log ("parser", GF_LOG_ERROR, "invalid argument name", name);
    return -1;
  }

  node->ctx = gctx;
  node->name = name;
  node->next = complete_tree;
  if (complete_tree)
    complete_tree->prev = node;
  node->options = get_new_dict ();
  complete_tree = node;

  tree = node;
  gf_log ("parser", GF_LOG_DEBUG, "New node for '%s'", name);

  return 0;
}

static int
section_type (char *type)
{
  int32_t ret = -1;
  if (!type) {
    gf_log ("parser", GF_LOG_ERROR, "invalid argument type");
    return -1;
  }

  ret = xlator_set_type (tree, type);
  if (ret) {
    gf_log ("parser", GF_LOG_ERROR, "failed to set the node type");
    return -1;
  }

  gf_log ("parser", GF_LOG_DEBUG, "Type:%s:%s", tree->name, type);

  return 0;
}

static int 
section_option_cmd (char *key, char *cmd)
{
  char cmd_output[1024] = {0,};
  FILE *fpp = NULL;
  if (!key || !cmd){
    gf_log ("parser", GF_LOG_ERROR, "invalid argument");
    return -1;
  }
  fpp = popen (cmd, "r");
  if (!fpp)
    {
      gf_log ("parser", GF_LOG_ERROR, "%s: failed to popen", cmd);
      return -1;
    }
  if (!fgets (cmd_output, 1024, fpp))
    {
      gf_log ("parser", GF_LOG_ERROR, "failed to read output of cmd (%s)", cmd);
      return -1;
    }
  pclose (fpp);
  
  dict_set (tree->options, key, str_to_data (cmd_output));
  gf_log ("parser", GF_LOG_DEBUG, "Option:%s:%s:%s",
	  tree->name, key, cmd_output);

  return 0;
}

static int 
section_option (char *key, char *value)
{
  if (!key || !value){
    gf_log ("parser", GF_LOG_ERROR, "invalid argument");
    return -1;
  }
  dict_set (tree->options, key, str_to_data (value));
  gf_log ("parser", GF_LOG_DEBUG, "Option:%s:%s:%s",
	  tree->name, key, value);

  return 0;
}

static int 
section_sub (char *sub)
{
  xlator_t *trav = complete_tree;
  xlator_list_t *xlchild, *tmp, *xlparent;

  if (!sub) {
    gf_log ("parser", GF_LOG_ERROR, "invalid argument sub");
    return -1;
  }

  while (trav) {
    if (!strcmp (sub,  trav->name))
      break;
    trav = trav->next;
  }
  if (!trav) {
    gf_log ("parser", GF_LOG_ERROR, "no such subvolume: %s", sub);
    return -1;
  }
  
  if (trav == tree) {
    gf_log ("parser", GF_LOG_ERROR, "%s has %s as subvolume", sub, sub);
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

  gf_log ("parser", GF_LOG_DEBUG, "child:%s->%s", tree->name, sub);

  return 0;
}

static int
section_end (void)
{
  if (!tree->fops || !tree->mops) {
    gf_log ("parser", GF_LOG_ERROR, 
	    "\"type\" not specified for volume %s", tree->name);
    return -1;
  }
  gf_log ("parser", GF_LOG_DEBUG, "end:%s", tree->name);

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
  cut_tree (tree);
  complete_tree = NULL;
  gf_log ("parser", GF_LOG_ERROR,
	  "%s (text which caused syntax error: %s)", str, yytext);
  return 0;
}

extern FILE *yyin;
xlator_t *
file_to_xlator_tree (glusterfs_ctx_t *ctx,
		     FILE *fp)
{
  int ret = 0;
  gctx = ctx;
  yyin = fp;
  ret = yyparse ();
  
  if (1 == ret) {
    gf_log ("parser", GF_LOG_ERROR, "parsing of volume spec file failed, please review it once more");
    return NULL;
  }

  return complete_tree;
}
