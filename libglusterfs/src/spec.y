/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


%token SECTION_BEGIN SECTION_END OPTION NEWLINE SUBSECTION ID WHITESPACE COMMENT TYPE

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "xlator.h"

static int new_section (char *name);
static int section_type (char *type);
static int section_option (char *key, char *value);
static int section_sub (char *sub);
static int section_end (void);

#define YYSTYPE char *
int yyerror (const char *);
int yylex ();
%}


%%
SECTIONS: SECTION | SECTIONS SECTION;

SECTION: SECTION_HEADER SECTION_DATA SECTION_FOOTER;
SECTION_HEADER: SECTION_BEGIN ID {new_section ($2);};
SECTION_FOOTER: SECTION_END {section_end ();};

SECTION_DATA: SECTION_LINE | SECTION_DATA SECTION_LINE;
SECTION_LINE: OPTION_LINE | TYPE_LINE | SUBSECTION_LINE;

TYPE_LINE: TYPE ID {section_type ($2);};

OPTION_LINE: OPTION ID ID {section_option ($2, $3);};

SUBSECTION_LINE: SUBSECTION IDS;
IDS: ID {section_sub ($1);}| IDS ID {section_sub ($2);};
%%

struct xlator *complete_tree = NULL;
struct xlator *tree = NULL;

static int
cut_tree (struct xlator *tree)
{
  if (!tree){
    gf_log ("glusterfs", GF_LOG_ERROR, "spec.y->cut_tree: invalid argument tree\n");
    return -1;
  }
  struct xlator *trav = tree, *prev = tree;
  
  gf_log ("glusterfs", GF_LOG_ERROR, "translator tree cut\n");
  while (prev) {
    trav = prev->next;
    dict_destroy (prev->options);
    free (prev->name);
    free (prev);
    prev = trav;
  }
  
  return 0;
}

static int
new_section (char *name)
{
  struct xlator *node = (void *) calloc (1, sizeof (*node));

  if (!name) {
    gf_log ("glusterfs", GF_LOG_ERROR, "spec.y->new_secton: invalid argument name\n", name);
    return -1;
  }

  node->name = name;
  node->next = complete_tree;
  node->options = get_new_dict ();
  complete_tree = node;

  tree = node;

  gf_log ("glusterfs", GF_LOG_DEBUG, "spec.y->new_section: New node for '%s'\n", name);
  return 0;
}

static int
section_type (char *type)
{
  if (!type) {
    gf_log ("glusterfs", GF_LOG_ERROR, "spec.y->section_type: invalid argument type");
    return -1;
  }
  gf_log ("glusterfs", GF_LOG_DEBUG, "spec.y->section_type: Type:%s:%s\n", tree->name, type);
  xlator_set_type (tree, type);
  return 0;
}

static int 
section_option (char *key, char *value)
{
  if (!key || !value){
    gf_log ("libglusterfs", GF_LOG_ERROR, "spec.y->section_option: invalid argument\n");
    return -1;
  }
  dict_set (tree->options, key, str_to_data (value));
  gf_log ("libglusterfs", GF_LOG_DEBUG, "spec.y->section_option: Option:%s:%s:%s\n", 
	  tree->name, key, value);
  return 0;
}

static int 
section_sub (char *sub)
{
  struct xlator *trav = complete_tree;
  
  if (!sub) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "spec.y->section_sub: invalid argument sub\n");
    return -1;
  }
  while (trav) {
    if (!strcmp (sub,  trav->name))
      break;
    trav = trav->next;
  }
  if (!trav) {
    gf_log ("libglusterfs",GF_LOG_ERROR,  "spec.y->section_sub: no such node: %s\n", sub);
    return -1;
  }

  trav->parent = tree;
  trav->next_sibling = tree->first_child;
  tree->first_child = trav;
  
  gf_log ("liglusterfs", GF_LOG_DEBUG, "spec.y->section_sub: child:%s->%s\n", tree->name,  sub);
  return 0;
}

static int
section_end (void)
{
  gf_log ("libglusterfs", GF_LOG_DEBUG, "spec.y->section_end: end:%s\n", tree->name);
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
  cut_tree (tree);
  complete_tree = NULL;
  gf_log ("glusterfs", GF_LOG_ERROR, "yyerror: %s\n", str);
  return 0;
}

extern FILE *yyin;
struct xlator *
file_to_xlator_tree (FILE *fp)
{
  yyin = fp;
  yyparse ();
  return complete_tree;
}
