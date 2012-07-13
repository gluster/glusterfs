/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "common-utils.h"
#include "trie.h"

#define DISTANCE_EDIT 1
#define DISTANCE_INS  1
#define DISTANCE_DEL  1


struct trienode {
        char             id;
        char             eow;
        int              depth;
        void            *data;
        struct trie     *trie;
        struct trienode *parent;
        struct trienode *subnodes[255];
};

struct trie {
        struct trienode   root;
        int               nodecnt;
        size_t            len;
};


trie_t *
trie_new ()
{
        trie_t *trie = NULL;

        trie = GF_CALLOC (1, sizeof (*trie),  gf_common_mt_trie_trie);
        if (!trie)
                return NULL;

        trie->root.trie = trie;

        return trie;
}


static trienode_t *
trie_subnode (trienode_t *node, int id)
{
        trienode_t *subnode = NULL;

        subnode = node->subnodes[id];
        if (!subnode) {
                subnode = GF_CALLOC (1, sizeof (*subnode),
                                     gf_common_mt_trie_node);
                if (!subnode)
                        return NULL;

                subnode->id        = id;
                subnode->depth     = node->depth + 1;
                node->subnodes[id] = subnode;
                subnode->parent    = node;
                subnode->trie      = node->trie;
                node->trie->nodecnt++;
        }

        return subnode;
}


int
trie_add (trie_t *trie, const char *dword)
{
        trienode_t *node = NULL;
        int              i = 0;
        char             id = 0;
        trienode_t *subnode = NULL;

        node = &trie->root;

        for (i = 0; i < strlen (dword); i++) {
                id = dword[i];

                subnode = trie_subnode (node, id);
                if (!subnode)
                        return -1;
                node = subnode;
        }

        node->eow = 1;

        return 0;
}

static void
trienode_free (trienode_t *node)
{
        trienode_t *trav = NULL;
        int              i = 0;

        for (i = 0; i < 255; i++) {
                trav = node->subnodes[i];

                if (trav)
                        trienode_free (trav);
        }

        GF_FREE (node->data);
        GF_FREE (node);
}


void
trie_destroy (trie_t *trie)
{
        trienode_free ((trienode_t *)trie);
}


void
trie_destroy_bynode (trienode_t *node)
{
        trie_destroy (node->trie);
}


static int
trienode_walk (trienode_t *node, int (*fn)(trienode_t *node, void *data),
               void *data, int eowonly)
{
        trienode_t *trav = NULL;
        int              i = 0;
        int              cret = 0;
        int              ret = 0;

        if (!eowonly || node->eow)
                ret = fn (node, data);

        if (ret)
                goto out;

        for (i = 0; i < 255; i++) {
                trav = node->subnodes[i];
                if (!trav)
                        continue;

                cret = trienode_walk (trav, fn, data, eowonly);
                if (cret < 0) {
                        ret = cret;
                        goto out;
                }
                ret += cret;
        }

out:
        return ret;
}


static int
trie_walk (trie_t *trie, int (*fn)(trienode_t *node, void *data),
           void *data, int eowonly)
{
        return trienode_walk (&trie->root, fn, data, eowonly);
}


static void
print_node (trienode_t *node, char **buf)
{
        if (!node->parent)
                return;

        if (node->parent) {
                print_node (node->parent, buf);
                *(*buf)++ = node->id;
        }
}


int
trienode_get_word (trienode_t *node, char **bufp)
{
        char *buf = NULL;

        buf = GF_CALLOC (1, node->depth + 1, gf_common_mt_trie_buf);
        if (!buf)
                return -1;
        *bufp = buf;

        print_node (node, &buf);

        return 0;
}


static int
calc_dist (trienode_t *node, void *data)
{
        const char *word = NULL;
        int         i = 0;
        int        *row = NULL;
        int        *uprow = NULL;
        int         distu = 0;
        int         distl = 0;
        int         distul = 0;

        word = data;

        node->data = GF_CALLOC (node->trie->len, sizeof (int),
                                gf_common_mt_trie_data);
        if (!node->data)
                return -1;
        row = node->data;

        if (!node->parent) {
                for (i = 0; i < node->trie->len; i++)
                        row[i] = i+1;

                return 0;
        }

        uprow = node->parent->data;

        distu = node->depth;          /* up node */
        distul = node->parent->depth; /* up-left node */

        for (i = 0; i < node->trie->len; i++) {
                distl = uprow[i];     /* left node */

                if (word[i] == node->id)
                        row[i] = distul;
                else
                        row[i] = min ((distul + DISTANCE_EDIT),
                                      min ((distu + DISTANCE_DEL),
                                           (distl + DISTANCE_INS)));

                distu  = row[i];
                distul = distl;
        }

        return 0;
}


int
trienode_get_dist (trienode_t *node)
{
        int *row = NULL;

        row = node->data;

        return row[node->trie->len - 1];
}


struct trienodevec_w {
        struct trienodevec *vec;
        const char *word;
};


static void
trienodevec_clear (struct trienodevec *nodevec)
{
        memset(nodevec->nodes, 0, sizeof (*nodevec->nodes) * nodevec->cnt);
}


static int
collect_closest (trienode_t *node, void *data)
{
        struct trienodevec_w *nodevec_w = NULL;
        struct trienodevec *nodevec = NULL;
        int dist = 0;
        int i = 0;

        nodevec_w = data;
        nodevec = nodevec_w->vec;

        if (calc_dist (node, (void *)nodevec_w->word))
                return -1;

        if (!node->eow || !nodevec->cnt)
                return 0;

        dist = trienode_get_dist (node);

        /*
         * I thought that when descending further after some dictionary word dw,
         * if we see that child's distance is bigger than it was for dw, then we
         * can prune this branch, as it can contain only worse nodes.
         *
         * This conjecture fails, see eg:
         *
         * d("AB", "B") = 1;
         * d("AB", "BA") = 2;
         * d("AB", "BAB") = 1;
         *
         * -- if both "B" and "BAB" are in dict., then pruning at "BA" * would
         * miss "BAB".
         *
         * (example courtesy of Richard Bann <richardbann at gmail.com>)

        if (node->parent->eow && dist > trienode_get_dist (node->parent))
                return 1;

         */

        if (nodevec->nodes[0] &&
            dist < trienode_get_dist (nodevec->nodes[0])) {
                /* improving over the findings so far */
                trienodevec_clear (nodevec);
                nodevec->nodes[0] = node;
        } else if (!nodevec->nodes[0] ||
                   dist == trienode_get_dist (nodevec->nodes[0])) {
                /* as good as the best so far, add if there is free space */
                for (i = 0; i < nodevec->cnt; i++) {
                        if (!nodevec->nodes[i]) {
                                nodevec->nodes[i] = node;
                                break;
                        }
                }
        }

        return 0;
}


int
trie_measure (trie_t *trie, const char *word, trienode_t **nodes,
              int nodecnt)
{
        struct trienodevec nodevec = {0,};

        nodevec.nodes = nodes;
        nodevec.cnt = nodecnt;

        return trie_measure_vec (trie, word, &nodevec);
}


int
trie_measure_vec (trie_t *trie, const char *word, struct trienodevec *nodevec)
{
        struct trienodevec_w nodevec_w = {0,};
        int ret = 0;

        trie->len = strlen (word);

        trienodevec_clear (nodevec);
        nodevec_w.vec = nodevec;
        nodevec_w.word = word;

        ret = trie_walk (trie, collect_closest, &nodevec_w, 0);
        if (ret > 0)
                ret = 0;

        return ret;
}


static int
trienode_reset (trienode_t *node, void *data)
{
        GF_FREE (node->data);

        return 0;
}


void
trie_reset_search (trie_t *trie)
{
        trie->len = 0;

        trie_walk (trie, trienode_reset, NULL, 0);
}
