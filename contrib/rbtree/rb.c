/* Produced by texiweb from libavl.w. */

/* libavl - library for manipulation of binary trees.
   Copyright (C) 1998-2002, 2004 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
   See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   The author may be contacted at <blp@gnu.org> on the Internet, or
   write to Ben Pfaff, Stanford University, Computer Science Dept., 353
   Serra Mall, Stanford CA 94305, USA.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rb.h"

/* Creates and returns a new table
   with comparison function |compare| using parameter |param|
   and memory allocator |allocator|.
   Returns |NULL| if memory allocation failed. */
struct rb_table *
rb_create (rb_comparison_func *compare, void *param,
            struct libavl_allocator *allocator)
{
  struct rb_table *tree;

  assert (compare != NULL);

  if (allocator == NULL)
    allocator = &rb_allocator_default;

  tree = allocator->libavl_malloc (allocator, sizeof *tree);
  if (tree == NULL)
    return NULL;

  tree->rb_root = NULL;
  tree->rb_compare = compare;
  tree->rb_param = param;
  tree->rb_alloc = allocator;
  tree->rb_count = 0;
  tree->rb_generation = 0;

  return tree;
}

/* Search |tree| for an item matching |item|, and return it if found.
   Otherwise return |NULL|. */
void *
rb_find (const struct rb_table *tree, const void *item)
{
  const struct rb_node *p;

  assert (tree != NULL && item != NULL);
  for (p = tree->rb_root; p != NULL; )
    {
      int cmp = tree->rb_compare (item, p->rb_data, tree->rb_param);

      if (cmp < 0)
        p = p->rb_link[0];
      else if (cmp > 0)
        p = p->rb_link[1];
      else /* |cmp == 0| */
        return p->rb_data;
    }

  return NULL;
}

/* Inserts |item| into |tree| and returns a pointer to |item|'s address.
   If a duplicate item is found in the tree,
   returns a pointer to the duplicate without inserting |item|.
   Returns |NULL| in case of memory allocation failure. */
void **
rb_probe (struct rb_table *tree, void *item)
{
  struct rb_node *pa[RB_MAX_HEIGHT]; /* Nodes on stack. */
  unsigned char da[RB_MAX_HEIGHT];   /* Directions moved from stack nodes. */
  int k;                             /* Stack height. */

  struct rb_node *p; /* Traverses tree looking for insertion point. */
  struct rb_node *n; /* Newly inserted node. */

  assert (tree != NULL && item != NULL);

  pa[0] = (struct rb_node *) &tree->rb_root;
  da[0] = 0;
  k = 1;
  for (p = tree->rb_root; p != NULL; p = p->rb_link[da[k - 1]])
    {
      int cmp = tree->rb_compare (item, p->rb_data, tree->rb_param);
      if (cmp == 0)
        return &p->rb_data;

      pa[k] = p;
      da[k++] = cmp > 0;
    }

  n = pa[k - 1]->rb_link[da[k - 1]] =
    tree->rb_alloc->libavl_malloc (tree->rb_alloc, sizeof *n);
  if (n == NULL)
    return NULL;

  n->rb_data = item;
  n->rb_link[0] = n->rb_link[1] = NULL;
  n->rb_color = RB_RED;
  tree->rb_count++;
  tree->rb_generation++;

  while (k >= 3 && pa[k - 1]->rb_color == RB_RED)
    {
      if (da[k - 2] == 0)
        {
          struct rb_node *y = pa[k - 2]->rb_link[1];
          if (y != NULL && y->rb_color == RB_RED)
            {
              pa[k - 1]->rb_color = y->rb_color = RB_BLACK;
              pa[k - 2]->rb_color = RB_RED;
              k -= 2;
            }
          else
            {
              struct rb_node *x;

              if (da[k - 1] == 0)
                y = pa[k - 1];
              else
                {
                  x = pa[k - 1];
                  y = x->rb_link[1];
                  x->rb_link[1] = y->rb_link[0];
                  y->rb_link[0] = x;
                  pa[k - 2]->rb_link[0] = y;
                }

              x = pa[k - 2];
              x->rb_color = RB_RED;
              y->rb_color = RB_BLACK;

              x->rb_link[0] = y->rb_link[1];
              y->rb_link[1] = x;
              pa[k - 3]->rb_link[da[k - 3]] = y;
              break;
            }
        }
      else
        {
          struct rb_node *y = pa[k - 2]->rb_link[0];
          if (y != NULL && y->rb_color == RB_RED)
            {
              pa[k - 1]->rb_color = y->rb_color = RB_BLACK;
              pa[k - 2]->rb_color = RB_RED;
              k -= 2;
            }
          else
            {
              struct rb_node *x;

              if (da[k - 1] == 1)
                y = pa[k - 1];
              else
                {
                  x = pa[k - 1];
                  y = x->rb_link[0];
                  x->rb_link[0] = y->rb_link[1];
                  y->rb_link[1] = x;
                  pa[k - 2]->rb_link[1] = y;
                }

              x = pa[k - 2];
              x->rb_color = RB_RED;
              y->rb_color = RB_BLACK;

              x->rb_link[1] = y->rb_link[0];
              y->rb_link[0] = x;
              pa[k - 3]->rb_link[da[k - 3]] = y;
              break;
            }
        }
    }
  tree->rb_root->rb_color = RB_BLACK;


  return &n->rb_data;
}

/* Inserts |item| into |table|.
   Returns |NULL| if |item| was successfully inserted
   or if a memory allocation error occurred.
   Otherwise, returns the duplicate item. */
void *
rb_insert (struct rb_table *table, void *item)
{
  void **p = rb_probe (table, item);
  return p == NULL || *p == item ? NULL : *p;
}

/* Inserts |item| into |table|, replacing any duplicate item.
   Returns |NULL| if |item| was inserted without replacing a duplicate,
   or if a memory allocation error occurred.
   Otherwise, returns the item that was replaced. */
void *
rb_replace (struct rb_table *table, void *item)
{
  void **p = rb_probe (table, item);
  if (p == NULL || *p == item)
    return NULL;
  else
    {
      void *r = *p;
      *p = item;
      return r;
    }
}

/* Deletes from |tree| and returns an item matching |item|.
   Returns a null pointer if no matching item found. */
void *
rb_delete (struct rb_table *tree, const void *item)
{
  struct rb_node *pa[RB_MAX_HEIGHT]; /* Nodes on stack. */
  unsigned char da[RB_MAX_HEIGHT];   /* Directions moved from stack nodes. */
  int k;                             /* Stack height. */

  struct rb_node *p;    /* The node to delete, or a node part way to it. */
  int cmp;              /* Result of comparison between |item| and |p|. */

  assert (tree != NULL && item != NULL);

  k = 0;
  p = (struct rb_node *) &tree->rb_root;
  for (cmp = -1; cmp != 0;
       cmp = tree->rb_compare (item, p->rb_data, tree->rb_param))
    {
      int dir = cmp > 0;

      pa[k] = p;
      da[k++] = dir;

      p = p->rb_link[dir];
      if (p == NULL)
        return NULL;
    }
  item = p->rb_data;

  if (p->rb_link[1] == NULL)
    pa[k - 1]->rb_link[da[k - 1]] = p->rb_link[0];
  else
    {
      enum rb_color t;
      struct rb_node *r = p->rb_link[1];

      if (r->rb_link[0] == NULL)
        {
          r->rb_link[0] = p->rb_link[0];
          t = r->rb_color;
          r->rb_color = p->rb_color;
          p->rb_color = t;
          pa[k - 1]->rb_link[da[k - 1]] = r;
          da[k] = 1;
          pa[k++] = r;
        }
      else
        {
          struct rb_node *s;
          int j = k++;

          for (;;)
            {
              da[k] = 0;
              pa[k++] = r;
              s = r->rb_link[0];
              if (s->rb_link[0] == NULL)
                break;

              r = s;
            }

          da[j] = 1;
          pa[j] = s;
          pa[j - 1]->rb_link[da[j - 1]] = s;

          s->rb_link[0] = p->rb_link[0];
          r->rb_link[0] = s->rb_link[1];
          s->rb_link[1] = p->rb_link[1];

          t = s->rb_color;
          s->rb_color = p->rb_color;
          p->rb_color = t;
        }
    }

  if (p->rb_color == RB_BLACK)
    {
      for (;;)
        {
          struct rb_node *x = pa[k - 1]->rb_link[da[k - 1]];
          if (x != NULL && x->rb_color == RB_RED)
            {
              x->rb_color = RB_BLACK;
              break;
            }
          if (k < 2)
            break;

          if (da[k - 1] == 0)
            {
              struct rb_node *w = pa[k - 1]->rb_link[1];

              if (w->rb_color == RB_RED)
                {
                  w->rb_color = RB_BLACK;
                  pa[k - 1]->rb_color = RB_RED;

                  pa[k - 1]->rb_link[1] = w->rb_link[0];
                  w->rb_link[0] = pa[k - 1];
                  pa[k - 2]->rb_link[da[k - 2]] = w;

                  pa[k] = pa[k - 1];
                  da[k] = 0;
                  pa[k - 1] = w;
                  k++;

                  w = pa[k - 1]->rb_link[1];
                }

              if ((w->rb_link[0] == NULL
                   || w->rb_link[0]->rb_color == RB_BLACK)
                  && (w->rb_link[1] == NULL
                      || w->rb_link[1]->rb_color == RB_BLACK))
                w->rb_color = RB_RED;
              else
                {
                  if (w->rb_link[1] == NULL
                      || w->rb_link[1]->rb_color == RB_BLACK)
                    {
                      struct rb_node *y = w->rb_link[0];
                      y->rb_color = RB_BLACK;
                      w->rb_color = RB_RED;
                      w->rb_link[0] = y->rb_link[1];
                      y->rb_link[1] = w;
                      w = pa[k - 1]->rb_link[1] = y;
                    }

                  w->rb_color = pa[k - 1]->rb_color;
                  pa[k - 1]->rb_color = RB_BLACK;
                  w->rb_link[1]->rb_color = RB_BLACK;

                  pa[k - 1]->rb_link[1] = w->rb_link[0];
                  w->rb_link[0] = pa[k - 1];
                  pa[k - 2]->rb_link[da[k - 2]] = w;
                  break;
                }
            }
          else
            {
              struct rb_node *w = pa[k - 1]->rb_link[0];

              if (w->rb_color == RB_RED)
                {
                  w->rb_color = RB_BLACK;
                  pa[k - 1]->rb_color = RB_RED;

                  pa[k - 1]->rb_link[0] = w->rb_link[1];
                  w->rb_link[1] = pa[k - 1];
                  pa[k - 2]->rb_link[da[k - 2]] = w;

                  pa[k] = pa[k - 1];
                  da[k] = 1;
                  pa[k - 1] = w;
                  k++;

                  w = pa[k - 1]->rb_link[0];
                }

              if ((w->rb_link[0] == NULL
                   || w->rb_link[0]->rb_color == RB_BLACK)
                  && (w->rb_link[1] == NULL
                      || w->rb_link[1]->rb_color == RB_BLACK))
                w->rb_color = RB_RED;
              else
                {
                  if (w->rb_link[0] == NULL
                      || w->rb_link[0]->rb_color == RB_BLACK)
                    {
                      struct rb_node *y = w->rb_link[1];
                      y->rb_color = RB_BLACK;
                      w->rb_color = RB_RED;
                      w->rb_link[1] = y->rb_link[0];
                      y->rb_link[0] = w;
                      w = pa[k - 1]->rb_link[0] = y;
                    }

                  w->rb_color = pa[k - 1]->rb_color;
                  pa[k - 1]->rb_color = RB_BLACK;
                  w->rb_link[0]->rb_color = RB_BLACK;

                  pa[k - 1]->rb_link[0] = w->rb_link[1];
                  w->rb_link[1] = pa[k - 1];
                  pa[k - 2]->rb_link[da[k - 2]] = w;
                  break;
                }
            }

          k--;
        }

    }

  tree->rb_alloc->libavl_free (tree->rb_alloc, p);
  tree->rb_count--;
  tree->rb_generation++;
  return (void *) item;
}

/* Refreshes the stack of parent pointers in |trav|
   and updates its generation number. */
static void
trav_refresh (struct rb_traverser *trav)
{
  assert (trav != NULL);

  trav->rb_generation = trav->rb_table->rb_generation;

  if (trav->rb_node != NULL)
    {
      rb_comparison_func *cmp = trav->rb_table->rb_compare;
      void *param = trav->rb_table->rb_param;
      struct rb_node *node = trav->rb_node;
      struct rb_node *i;

      trav->rb_height = 0;
      for (i = trav->rb_table->rb_root; i != node; )
        {
          assert (trav->rb_height < RB_MAX_HEIGHT);
          assert (i != NULL);

          trav->rb_stack[trav->rb_height++] = i;
          i = i->rb_link[cmp (node->rb_data, i->rb_data, param) > 0];
        }
    }
}

/* Initializes |trav| for use with |tree|
   and selects the null node. */
void
rb_t_init (struct rb_traverser *trav, struct rb_table *tree)
{
  trav->rb_table = tree;
  trav->rb_node = NULL;
  trav->rb_height = 0;
  trav->rb_generation = tree->rb_generation;
}

/* Initializes |trav| for |tree|
   and selects and returns a pointer to its least-valued item.
   Returns |NULL| if |tree| contains no nodes. */
void *
rb_t_first (struct rb_traverser *trav, struct rb_table *tree)
{
  struct rb_node *x;

  assert (tree != NULL && trav != NULL);

  trav->rb_table = tree;
  trav->rb_height = 0;
  trav->rb_generation = tree->rb_generation;

  x = tree->rb_root;
  if (x != NULL)
    while (x->rb_link[0] != NULL)
      {
        assert (trav->rb_height < RB_MAX_HEIGHT);
        trav->rb_stack[trav->rb_height++] = x;
        x = x->rb_link[0];
      }
  trav->rb_node = x;

  return x != NULL ? x->rb_data : NULL;
}

/* Initializes |trav| for |tree|
   and selects and returns a pointer to its greatest-valued item.
   Returns |NULL| if |tree| contains no nodes. */
void *
rb_t_last (struct rb_traverser *trav, struct rb_table *tree)
{
  struct rb_node *x;

  assert (tree != NULL && trav != NULL);

  trav->rb_table = tree;
  trav->rb_height = 0;
  trav->rb_generation = tree->rb_generation;

  x = tree->rb_root;
  if (x != NULL)
    while (x->rb_link[1] != NULL)
      {
        assert (trav->rb_height < RB_MAX_HEIGHT);
        trav->rb_stack[trav->rb_height++] = x;
        x = x->rb_link[1];
      }
  trav->rb_node = x;

  return x != NULL ? x->rb_data : NULL;
}

/* Searches for |item| in |tree|.
   If found, initializes |trav| to the item found and returns the item
   as well.
   If there is no matching item, initializes |trav| to the null item
   and returns |NULL|. */
void *
rb_t_find (struct rb_traverser *trav, struct rb_table *tree, void *item)
{
  struct rb_node *p, *q;

  assert (trav != NULL && tree != NULL && item != NULL);
  trav->rb_table = tree;
  trav->rb_height = 0;
  trav->rb_generation = tree->rb_generation;
  for (p = tree->rb_root; p != NULL; p = q)
    {
      int cmp = tree->rb_compare (item, p->rb_data, tree->rb_param);

      if (cmp < 0)
        q = p->rb_link[0];
      else if (cmp > 0)
        q = p->rb_link[1];
      else /* |cmp == 0| */
        {
          trav->rb_node = p;
          return p->rb_data;
        }

      assert (trav->rb_height < RB_MAX_HEIGHT);
      trav->rb_stack[trav->rb_height++] = p;
    }

  trav->rb_height = 0;
  trav->rb_node = NULL;
  return NULL;
}

/* Attempts to insert |item| into |tree|.
   If |item| is inserted successfully, it is returned and |trav| is
   initialized to its location.
   If a duplicate is found, it is returned and |trav| is initialized to
   its location.  No replacement of the item occurs.
   If a memory allocation failure occurs, |NULL| is returned and |trav|
   is initialized to the null item. */
void *
rb_t_insert (struct rb_traverser *trav, struct rb_table *tree, void *item)
{
  void **p;

  assert (trav != NULL && tree != NULL && item != NULL);

  p = rb_probe (tree, item);
  if (p != NULL)
    {
      trav->rb_table = tree;
      trav->rb_node =
        ((struct rb_node *)
         ((char *) p - offsetof (struct rb_node, rb_data)));
      trav->rb_generation = tree->rb_generation - 1;
      return *p;
    }
  else
    {
      rb_t_init (trav, tree);
      return NULL;
    }
}

/* Initializes |trav| to have the same current node as |src|. */
void *
rb_t_copy (struct rb_traverser *trav, const struct rb_traverser *src)
{
  assert (trav != NULL && src != NULL);

  if (trav != src)
    {
      trav->rb_table = src->rb_table;
      trav->rb_node = src->rb_node;
      trav->rb_generation = src->rb_generation;
      if (trav->rb_generation == trav->rb_table->rb_generation)
        {
          trav->rb_height = src->rb_height;
          memcpy (trav->rb_stack, (const void *) src->rb_stack,
                  sizeof *trav->rb_stack * trav->rb_height);
        }
    }

  return trav->rb_node != NULL ? trav->rb_node->rb_data : NULL;
}

/* Returns the next data item in inorder
   within the tree being traversed with |trav|,
   or if there are no more data items returns |NULL|. */
void *
rb_t_next (struct rb_traverser *trav)
{
  struct rb_node *x;

  assert (trav != NULL);

  if (trav->rb_generation != trav->rb_table->rb_generation)
    trav_refresh (trav);

  x = trav->rb_node;
  if (x == NULL)
    {
      return rb_t_first (trav, trav->rb_table);
    }
  else if (x->rb_link[1] != NULL)
    {
      assert (trav->rb_height < RB_MAX_HEIGHT);
      trav->rb_stack[trav->rb_height++] = x;
      x = x->rb_link[1];

      while (x->rb_link[0] != NULL)
        {
          assert (trav->rb_height < RB_MAX_HEIGHT);
          trav->rb_stack[trav->rb_height++] = x;
          x = x->rb_link[0];
        }
    }
  else
    {
      struct rb_node *y;

      do
        {
          if (trav->rb_height == 0)
            {
              trav->rb_node = NULL;
              return NULL;
            }

          y = x;
          x = trav->rb_stack[--trav->rb_height];
        }
      while (y == x->rb_link[1]);
    }
  trav->rb_node = x;

  return x->rb_data;
}

/* Returns the previous data item in inorder
   within the tree being traversed with |trav|,
   or if there are no more data items returns |NULL|. */
void *
rb_t_prev (struct rb_traverser *trav)
{
  struct rb_node *x;

  assert (trav != NULL);

  if (trav->rb_generation != trav->rb_table->rb_generation)
    trav_refresh (trav);

  x = trav->rb_node;
  if (x == NULL)
    {
      return rb_t_last (trav, trav->rb_table);
    }
  else if (x->rb_link[0] != NULL)
    {
      assert (trav->rb_height < RB_MAX_HEIGHT);
      trav->rb_stack[trav->rb_height++] = x;
      x = x->rb_link[0];

      while (x->rb_link[1] != NULL)
        {
          assert (trav->rb_height < RB_MAX_HEIGHT);
          trav->rb_stack[trav->rb_height++] = x;
          x = x->rb_link[1];
        }
    }
  else
    {
      struct rb_node *y;

      do
        {
          if (trav->rb_height == 0)
            {
              trav->rb_node = NULL;
              return NULL;
            }

          y = x;
          x = trav->rb_stack[--trav->rb_height];
        }
      while (y == x->rb_link[0]);
    }
  trav->rb_node = x;

  return x->rb_data;
}

/* Returns |trav|'s current item. */
void *
rb_t_cur (struct rb_traverser *trav)
{
  assert (trav != NULL);

  return trav->rb_node != NULL ? trav->rb_node->rb_data : NULL;
}

/* Replaces the current item in |trav| by |new| and returns the item replaced.
   |trav| must not have the null item selected.
   The new item must not upset the ordering of the tree. */
void *
rb_t_replace (struct rb_traverser *trav, void *new)
{
  void *old;

  assert (trav != NULL && trav->rb_node != NULL && new != NULL);
  old = trav->rb_node->rb_data;
  trav->rb_node->rb_data = new;
  return old;
}

/* Destroys |new| with |rb_destroy (new, destroy)|,
   first setting right links of nodes in |stack| within |new|
   to null pointers to avoid touching uninitialized data. */
static void
copy_error_recovery (struct rb_node **stack, int height,
                     struct rb_table *new, rb_item_func *destroy)
{
  assert (stack != NULL && height >= 0 && new != NULL);

  for (; height > 2; height -= 2)
    stack[height - 1]->rb_link[1] = NULL;
  rb_destroy (new, destroy);
}

/* Copies |org| to a newly created tree, which is returned.
   If |copy != NULL|, each data item in |org| is first passed to |copy|,
   and the return values are inserted into the tree,
   with |NULL| return values taken as indications of failure.
   On failure, destroys the partially created new tree,
   applying |destroy|, if non-null, to each item in the new tree so far,
   and returns |NULL|.
   If |allocator != NULL|, it is used for allocation in the new tree.
   Otherwise, the same allocator used for |org| is used. */
struct rb_table *
rb_copy (const struct rb_table *org, rb_copy_func *copy,
          rb_item_func *destroy, struct libavl_allocator *allocator)
{
  struct rb_node *stack[2 * (RB_MAX_HEIGHT + 1)];
  int height = 0;

  struct rb_table *new;
  const struct rb_node *x;
  struct rb_node *y;

  assert (org != NULL);
  new = rb_create (org->rb_compare, org->rb_param,
                    allocator != NULL ? allocator : org->rb_alloc);
  if (new == NULL)
    return NULL;
  new->rb_count = org->rb_count;
  if (new->rb_count == 0)
    return new;

  x = (const struct rb_node *) &org->rb_root;
  y = (struct rb_node *) &new->rb_root;
  for (;;)
    {
      while (x->rb_link[0] != NULL)
        {
          assert (height < 2 * (RB_MAX_HEIGHT + 1));

          y->rb_link[0] =
            new->rb_alloc->libavl_malloc (new->rb_alloc,
                                           sizeof *y->rb_link[0]);
          if (y->rb_link[0] == NULL)
            {
              if (y != (struct rb_node *) &new->rb_root)
                {
                  y->rb_data = NULL;
                  y->rb_link[1] = NULL;
                }

              copy_error_recovery (stack, height, new, destroy);
              return NULL;
            }

          stack[height++] = (struct rb_node *) x;
          stack[height++] = y;
          x = x->rb_link[0];
          y = y->rb_link[0];
        }
      y->rb_link[0] = NULL;

      for (;;)
        {
          y->rb_color = x->rb_color;
          if (copy == NULL)
            y->rb_data = x->rb_data;
          else
            {
              y->rb_data = copy (x->rb_data, org->rb_param);
              if (y->rb_data == NULL)
                {
                  y->rb_link[1] = NULL;
                  copy_error_recovery (stack, height, new, destroy);
                  return NULL;
                }
            }

          if (x->rb_link[1] != NULL)
            {
              y->rb_link[1] =
                new->rb_alloc->libavl_malloc (new->rb_alloc,
                                               sizeof *y->rb_link[1]);
              if (y->rb_link[1] == NULL)
                {
                  copy_error_recovery (stack, height, new, destroy);
                  return NULL;
                }

              x = x->rb_link[1];
              y = y->rb_link[1];
              break;
            }
          else
            y->rb_link[1] = NULL;

          if (height <= 2)
            return new;

          y = stack[--height];
          x = stack[--height];
        }
    }
}

/* Frees storage allocated for |tree|.
   If |destroy != NULL|, applies it to each data item in inorder. */
void
rb_destroy (struct rb_table *tree, rb_item_func *destroy)
{
  struct rb_node *p, *q;

  assert (tree != NULL);

  for (p = tree->rb_root; p != NULL; p = q)
    if (p->rb_link[0] == NULL)
      {
        q = p->rb_link[1];
        if (destroy != NULL && p->rb_data != NULL)
          destroy (p->rb_data, tree->rb_param);
        tree->rb_alloc->libavl_free (tree->rb_alloc, p);
      }
    else
      {
        q = p->rb_link[0];
        p->rb_link[0] = q->rb_link[1];
        q->rb_link[1] = p;
      }

  tree->rb_alloc->libavl_free (tree->rb_alloc, tree);
}

/* Allocates |size| bytes of space using |malloc()|.
   Returns a null pointer if allocation fails. */
void *
rb_malloc (struct libavl_allocator *allocator, size_t size)
{
  assert (allocator != NULL && size > 0);
  return malloc (size);
}

/* Frees |block|. */
void
rb_free (struct libavl_allocator *allocator, void *block)
{
  assert (allocator != NULL && block != NULL);
  free (block);
}

/* Default memory allocator that uses |malloc()| and |free()|. */
struct libavl_allocator rb_allocator_default =
  {
    rb_malloc,
    rb_free
  };

#undef NDEBUG
#include <assert.h>

/* Asserts that |rb_insert()| succeeds at inserting |item| into |table|. */
void
(rb_assert_insert) (struct rb_table *table, void *item)
{
  void **p = rb_probe (table, item);
  assert (p != NULL && *p == item);
}

/* Asserts that |rb_delete()| really removes |item| from |table|,
   and returns the removed item. */
void *
(rb_assert_delete) (struct rb_table *table, void *item)
{
  void *p = rb_delete (table, item);
  assert (p != NULL);
  return p;
}

