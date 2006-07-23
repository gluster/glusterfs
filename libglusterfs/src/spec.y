
%token SECTION_BEGIN SECTION_END OPTION NEWLINE ID SUBSECTION ID WHITESPACE COMMENT TYPE

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "xlator.h"

static void new_section (char *name);
static void section_type (char *type);
static void section_option (char *key, char *value);
static void section_sub (char *sub);
static void section_end (void);

#define YYSTYPE char *
int yyerror (const char *);

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

static void
cut_tree (struct xlator *tree)
{
  struct xlator *trav = tree, *prev = tree;

  printf ("tree cut :(\n");
  while (prev) {
    trav = prev->next;
    dict_destroy (prev->options);
    free (prev->name);
    free (prev);
    prev = trav;
  }
}

static void
new_section (char *name)
{
  struct xlator *node = (void *) calloc (1, sizeof (*node));

  node->name = name;
  node->next = complete_tree;
  node->options = get_new_dict ();
  complete_tree = node;

  tree = node;

  printf ("New node for '%s'\n", name);
}

static void 
section_type (char *type)
{
  printf ("Type:%s:%s\n", tree->name, type);
  tree->fops = type_to_fops (type);
}

static void 
section_option (char *key, char *value)
{
  dict_set (tree->options, str_to_data (key), str_to_data (value));
  printf ("Option:%s:%s:%s\n", tree->name, key, value);
}

static void 
section_sub (char *sub)
{
  struct xlator *trav = complete_tree;

  while (trav) {
    if (!strcmp (sub,  trav->name))
      break;
    trav = trav->next;
  }
  if (!trav) {
    fprintf (stderr, "No such node: %s\n", sub);
    return;
  }

  trav->parent = tree;
  trav->next_sibling = tree->first_child;
  tree->first_child = trav;
  printf ("Child:%s->%s\n", tree->name,  sub);
}

static void
section_end (void)
{
  printf ("End:%s\n", tree->name);
  tree = NULL;
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
  fprintf (stderr, "error: %s\n", str);
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

