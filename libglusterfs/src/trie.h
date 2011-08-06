/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _TRIE_H_
#define _TRIE_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

struct trienode;
typedef struct trienode trienode_t;

struct trie;
typedef struct trie trie_t;

struct trienodevec {
        trienode_t **nodes;
        unsigned cnt;
};


trie_t *trie_new ();

int trie_add (trie_t *trie, const char *word);

void trie_destroy (trie_t *trie);

void trie_destroy_bynode (trienode_t *node);

int trie_measure (trie_t *trie, const char *word, trienode_t **nodes,
                  int nodecnt);

int trie_measure_vec (trie_t *trie, const char *word,
                      struct trienodevec *nodevec);

void trie_reset_search (trie_t *trie);

int trienode_get_dist (trienode_t *node);

int trienode_get_word (trienode_t *node, char **buf);

#endif
