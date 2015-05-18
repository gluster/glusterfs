/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _TRIE_H_
#define _TRIE_H_

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
