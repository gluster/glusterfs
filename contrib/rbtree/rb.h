/* Produced by texiweb from libavl.w. */

/* libavl - library for manipulation of binary trees.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2004 Free Software
   Foundation, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA.

   This code is also covered by the following earlier license notice:

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
*/

#ifndef RB_H
#define RB_H 1

#include <stddef.h>

/* Function types. */
typedef int rb_comparison_func (const void *rb_a, const void *rb_b,
                                 void *rb_param);
typedef void rb_item_func (void *rb_item, void *rb_param);
typedef void *rb_copy_func (void *rb_item, void *rb_param);

#ifndef LIBAVL_ALLOCATOR
#define LIBAVL_ALLOCATOR
/* Memory allocator. */
struct libavl_allocator
  {
    void *(*libavl_malloc) (struct libavl_allocator *, size_t libavl_size);
    void (*libavl_free) (struct libavl_allocator *, void *libavl_block);
  };
#endif

/* Default memory allocator. */
extern struct libavl_allocator rb_allocator_default;
void *rb_malloc (struct libavl_allocator *, size_t);
void rb_free (struct libavl_allocator *, void *);

/* Maximum RB height. */
#ifndef RB_MAX_HEIGHT
#define RB_MAX_HEIGHT 128
#endif

/* Tree data structure. */
struct rb_table
  {
    struct rb_node *rb_root;          /* Tree's root. */
    rb_comparison_func *rb_compare;   /* Comparison function. */
    void *rb_param;                    /* Extra argument to |rb_compare|. */
    struct libavl_allocator *rb_alloc; /* Memory allocator. */
    size_t rb_count;                   /* Number of items in tree. */
    unsigned long rb_generation;       /* Generation number. */
  };

/* Color of a red-black node. */
enum rb_color
  {
    RB_BLACK,   /* Black. */
    RB_RED      /* Red. */
  };

/* A red-black tree node. */
struct rb_node
  {
    struct rb_node *rb_link[2];   /* Subtrees. */
    void *rb_data;                /* Pointer to data. */
    unsigned char rb_color;       /* Color. */
  };

/* RB traverser structure. */
struct rb_traverser
  {
    struct rb_table *rb_table;        /* Tree being traversed. */
    struct rb_node *rb_node;          /* Current node in tree. */
    struct rb_node *rb_stack[RB_MAX_HEIGHT];
                                        /* All the nodes above |rb_node|. */
    size_t rb_height;                  /* Number of nodes in |rb_parent|. */
    unsigned long rb_generation;       /* Generation number. */
  };

/* Table functions. */
struct rb_table *rb_create (rb_comparison_func *, void *,
                              struct libavl_allocator *);
struct rb_table *rb_copy (const struct rb_table *, rb_copy_func *,
                            rb_item_func *, struct libavl_allocator *);
void rb_destroy (struct rb_table *, rb_item_func *);
void **rb_probe (struct rb_table *, void *);
void *rb_insert (struct rb_table *, void *);
void *rb_replace (struct rb_table *, void *);
void *rb_delete (struct rb_table *, const void *);
void *rb_find (const struct rb_table *, const void *);
void rb_assert_insert (struct rb_table *, void *);
void *rb_assert_delete (struct rb_table *, void *);

#define rb_count(table) ((size_t) (table)->rb_count)

/* Table traverser functions. */
void rb_t_init (struct rb_traverser *, struct rb_table *);
void *rb_t_first (struct rb_traverser *, struct rb_table *);
void *rb_t_last (struct rb_traverser *, struct rb_table *);
void *rb_t_find (struct rb_traverser *, struct rb_table *, void *);
void *rb_t_insert (struct rb_traverser *, struct rb_table *, void *);
void *rb_t_copy (struct rb_traverser *, const struct rb_traverser *);
void *rb_t_next (struct rb_traverser *);
void *rb_t_prev (struct rb_traverser *);
void *rb_t_cur (struct rb_traverser *);
void *rb_t_replace (struct rb_traverser *, void *);

#endif /* rb.h */
