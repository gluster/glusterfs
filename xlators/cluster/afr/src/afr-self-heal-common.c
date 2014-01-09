/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "xlator.h"
#include "byte-order.h"

#include "afr.h"
#include "afr-transaction.h"
#include "afr-self-heal-common.h"
#include "afr-self-heal.h"
#include "pump.h"

#define ADD_FMT_STRING(msg, off, sh_str, status, print_log)                 \
        do {                                                                \
                if (AFR_SELF_HEAL_NOT_ATTEMPTED != status) {                \
                        off += snprintf (msg + off, sizeof (msg) - off,     \
                                         " "sh_str" self heal %s,",         \
                                         get_sh_completion_status (status));\
                        print_log = 1;                                      \
                }                                                           \
        } while (0)

#define ADD_FMT_STRING_SYNC(msg, off, sh_str, status, print_log)            \
        do {                                                                \
                if (AFR_SELF_HEAL_SYNC_BEGIN == status ||                   \
                    AFR_SELF_HEAL_FAILED == status)  {                      \
                        off += snprintf (msg + off, sizeof (msg) - off,     \
                                         " "sh_str" self heal %s,",         \
                                         get_sh_completion_status (status));\
                        print_log = 1;                                      \
                }                                                           \
        } while (0)


void
afr_sh_reset (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        memset (sh->child_errno, 0,
                sizeof (*sh->child_errno) * priv->child_count);
        memset (sh->buf, 0, sizeof (*sh->buf) * priv->child_count);
        memset (sh->parentbufs, 0,
                sizeof (*sh->parentbufs) * priv->child_count);
        memset (sh->success, 0, sizeof (*sh->success) * priv->child_count);
        memset (sh->locked_nodes, 0,
                sizeof (*sh->locked_nodes) * priv->child_count);
        sh->active_sinks = 0;

        afr_reset_xattr (sh->xattr, priv->child_count);
}

//Intersection[child]=1 if child is part of intersection
void
afr_children_intersection_get (int32_t *set1, int32_t *set2,
                               int *intersection, unsigned int child_count)
{
        int                      i = 0;

        memset (intersection, 0, sizeof (*intersection) * child_count);
        for (i = 0; i < child_count; i++) {
                intersection[i] = afr_is_child_present (set1, child_count, i)
                                     && afr_is_child_present (set2, child_count,
                                                              i);
        }
}

/**
 * select_source - select a source and return it
 */

int
afr_sh_select_source (int sources[], int child_count)
{
        int i = 0;
        for (i = 0; i < child_count; i++)
                if (sources[i])
                        return i;

        return -1;
}

void
afr_sh_mark_source_sinks (call_frame_t *frame, xlator_t *this)
{
        int              i = 0;
        afr_local_t     *local      = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              active_sinks = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (sh->sources[i] == 0 && local->child_up[i] == 1) {
                        active_sinks++;
                        sh->success[i] = 1;
                } else if (sh->sources[i] == 1 && local->child_up[i] == 1) {
                        sh->success[i] = 1;
                }
        }
        sh->active_sinks = active_sinks;
}

int
afr_sh_source_count (int sources[], int child_count)
{
        int i = 0;
        int nsource = 0;

        for (i = 0; i < child_count; i++)
                if (sources[i])
                        nsource++;
        return nsource;
}

void
afr_sh_set_error (afr_self_heal_t *sh, int32_t op_errno)
{
        sh->op_ret = -1;
	sh->op_errno = afr_most_important_error(sh->op_errno, op_errno,
						_gf_false);
}

void
afr_sh_print_pending_matrix (int32_t *pending_matrix[], xlator_t *this)
{
        afr_private_t *  priv = this->private;
        char            *buf  = NULL;
        char            *ptr  = NULL;
        int              i    = 0;
        int              j    = 0;

        /* 10 digits per entry + 1 space + '[' and ']' */
        buf = GF_MALLOC (priv->child_count * 11 + 8, gf_afr_mt_char);

        for (i = 0; i < priv->child_count; i++) {
                ptr = buf;
                ptr += sprintf (ptr, "[ ");
                for (j = 0; j < priv->child_count; j++) {
                        ptr += sprintf (ptr, "%d ", pending_matrix[i][j]);
                }
                sprintf (ptr, "]");
                gf_log (this->name, GF_LOG_DEBUG, "pending_matrix: %s", buf);
        }

        GF_FREE (buf);
}

char*
afr_get_pending_matrix_str (int32_t *pending_matrix[], xlator_t *this)
{
        afr_private_t *  priv = this->private;
        char            *buf  = NULL;
        char            *ptr  = NULL;
        int              i    = 0;
        int              j    = 0;
        int             child_count = priv->child_count;
        char            *matrix_begin = "[ [ ";
        char            *matrix_end = "] ]";
        char            *seperator = "] [ ";
        int             pending_entry_strlen = 12; //Including space after entry
        int             matrix_begin_strlen = 0;
        int             matrix_end_strlen = 0;
        int             seperator_strlen = 0;
        int             string_length = 0;
        char            *msg = "- Pending matrix:  ";

        /*
         *  for a list of lists of [ [ a b ] [ c d ] ]
         * */

        matrix_begin_strlen = strlen (matrix_begin);
        matrix_end_strlen = strlen (matrix_end);
        seperator_strlen = strlen (seperator);
        string_length = matrix_begin_strlen + matrix_end_strlen
                        + (child_count -1) * seperator_strlen
                        + (child_count * child_count * pending_entry_strlen);

        buf = GF_CALLOC (1, 1 + strlen (msg) + string_length , gf_afr_mt_char);
        if (!buf)
                goto out;

        ptr = buf;
        ptr += sprintf (ptr, "%s", msg);
        ptr += sprintf (ptr, "%s", matrix_begin);
        for (i = 0; i < priv->child_count; i++) {
                for (j = 0; j < priv->child_count; j++) {
                        ptr += sprintf (ptr, "%d ", pending_matrix[i][j]);
                }
                if (i < priv->child_count -1)
                        ptr += sprintf (ptr, "%s", seperator);
        }

        ptr += sprintf (ptr, "%s", matrix_end);

out:
        return buf;
}

void
afr_sh_print_split_brain_log (int32_t *pending_matrix[], xlator_t *this,
                              const char *loc)
{
        char *buf      = NULL;
        char *free_ptr = NULL;

        buf = afr_get_pending_matrix_str (pending_matrix, this);
        if (buf)
                free_ptr = buf;
        else
                buf = "";


        gf_log (this->name, GF_LOG_ERROR, "Unable to self-heal contents of '%s'"
                " (possible split-brain). Please delete the file from all but "
                "the preferred subvolume.%s", loc, buf);
        GF_FREE (free_ptr);
        return;
}


void
afr_init_pending_matrix (int32_t **pending_matrix, size_t child_count)
{
        int             i   = 0;
        int             j   = 0;

        GF_ASSERT (pending_matrix);

        for (i = 0; i < child_count; i++) {
                for (j = 0; j < child_count; j++) {
                        pending_matrix[i][j] = 0;
                }
        }
}

void
afr_mark_ignorant_subvols_as_pending (int32_t **pending_matrix,
                                      unsigned char *ignorant_subvols,
                                      size_t  child_count)
{
        int            i                = 0;
        int            j                = 0;

        GF_ASSERT (pending_matrix);
        GF_ASSERT (ignorant_subvols);

        for (i = 0; i < child_count; i++) {
                if (ignorant_subvols[i]) {
                        for (j = 0; j < child_count; j++) {
                                if (!ignorant_subvols[j])
                                        pending_matrix[j][i] += 1;
                        }
                }
        }
}

int
afr_build_pending_matrix (char **pending_key, int32_t **pending_matrix,
                          unsigned char *ignorant_subvols,
                          dict_t *xattr[], afr_transaction_type type,
                          size_t child_count)
{
        /* Indexable by result of afr_index_for_transaction_type(): 0 -- 2. */
        int32_t        pending[3]       = {0,};
        void          *pending_raw      = NULL;
        int            ret              = -1;
        int            i                = 0;
        int            j                = 0;
        int            k                = 0;

        afr_init_pending_matrix (pending_matrix, child_count);

        for (i = 0; i < child_count; i++) {
                pending_raw = NULL;

                for (j = 0; j < child_count; j++) {
                        ret = dict_get_ptr (xattr[i], pending_key[j],
                                            &pending_raw);

                        if (ret != 0) {
                                /*
                                 * There is no xattr present. This means this
                                 * subvolume should be considered an 'ignorant'
                                 * subvolume.
                                 */

                                if (ignorant_subvols)
                                        ignorant_subvols[i] = 1;
                                continue;
                        }

                        memcpy (pending, pending_raw, sizeof(pending));
                        k = afr_index_for_transaction_type (type);

                        pending_matrix[i][j] = ntoh32 (pending[k]);
                }
        }

        return ret;
}

typedef enum {
        AFR_NODE_INVALID,
        AFR_NODE_INNOCENT,
        AFR_NODE_FOOL,
        AFR_NODE_WISE,
} afr_node_type;

typedef struct {
        afr_node_type type;
        int           wisdom;
} afr_node_character;


static int
afr_sh_is_innocent (int32_t *array, int child_count)
{
        int i   = 0;
        int ret = 1;   /* innocent until proven guilty */

        for (i = 0; i < child_count; i++) {
                if (array[i]) {
                        ret = 0;
                        break;
                }
        }

        return ret;
}


static int
afr_sh_is_fool (int32_t *array, int i, int child_count)
{
        return array[i];   /* fool if accuses itself */
}


static int
afr_sh_is_wise (int32_t *array, int i, int child_count)
{
        return !array[i];  /* wise if does not accuse itself */
}


static int
afr_sh_all_nodes_innocent (afr_node_character *characters,
                           int child_count)
{
        int i   = 0;
        int ret = 1;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type != AFR_NODE_INNOCENT) {
                        ret = 0;
                        break;
                }
        }

        return ret;
}

static gf_boolean_t
afr_sh_has_any_fools (afr_node_character *characters, int child_count)
{
        int i   = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type == AFR_NODE_FOOL) {
                        return _gf_true;
                }
        }

        return _gf_false;
}


static int
afr_sh_wise_nodes_exist (afr_node_character *characters, int child_count)
{
        int i   = 0;
        int ret = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type == AFR_NODE_WISE) {
                        ret = 1;
                        break;
                }
        }

        return ret;
}


/*
 * The 'wisdom' of a wise node is 0 if any other wise node accuses it.
 * It is 1 if no other wise node accuses it.
 * Only wise nodes with wisdom 1 are sources.
 *
 * If no nodes with wisdom 1 exist, a split-brain has occurred.
 */

static void
afr_sh_compute_wisdom (int32_t *pending_matrix[],
                       afr_node_character characters[], int child_count)
{
        int i = 0;
        int j = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type == AFR_NODE_WISE) {
                        characters[i].wisdom = 1;

                        for (j = 0; j < child_count; j++) {
                                if ((characters[j].type == AFR_NODE_WISE)
                                    && pending_matrix[j][i]) {

                                        characters[i].wisdom = 0;
                                }
                        }
                }
        }
}


static int
afr_sh_wise_nodes_conflict (afr_node_character *characters,
                            int child_count)
{
        int i   = 0;
        int ret = 1;

        for (i = 0; i < child_count; i++) {
                if ((characters[i].type == AFR_NODE_WISE)
                    && characters[i].wisdom == 1) {

                        /* There is atleast one bona-fide wise node */
                        ret = 0;
                        break;
                }
        }

        return ret;
}


static int
afr_sh_mark_wisest_as_sources (int sources[],
                               afr_node_character *characters,
                               int child_count)
{
        int nsources = 0;
        int i        = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].wisdom == 1) {
                        sources[i] = 1;
                        nsources++;
                }
        }

        return nsources;
}

static void
afr_compute_witness_of_fools (int32_t *witnesses, int32_t **pending_matrix,
                              afr_node_character *characters,
                              int32_t child_count)
{
        int i       = 0;
        int j       = 0;
        int witness = 0;

        GF_ASSERT (witnesses);
        GF_ASSERT (pending_matrix);
        GF_ASSERT (characters);
        GF_ASSERT (child_count > 0);

        for (i = 0; i < child_count; i++) {
                if (characters[i].type != AFR_NODE_FOOL)
                        continue;

                witness = 0;
                for (j = 0; j < child_count; j++) {
                        if (i == j)
                                continue;
                        witness += pending_matrix[i][j];
                }
                witnesses[i] = witness;
        }
}

static int32_t
afr_find_biggest_witness_among_fools (int32_t *witnesses,
                                      afr_node_character *characters,
                                      int32_t child_count)
{
        int i               = 0;
        int biggest_witness = -1;
        int biggest_witness_idx = -1;
        int biggest_witness_cnt = -1;

        GF_ASSERT (witnesses);
        GF_ASSERT (characters);
        GF_ASSERT (child_count > 0);

        for (i = 0; i < child_count; i++) {
                if (characters[i].type != AFR_NODE_FOOL)
                        continue;

                if (biggest_witness < witnesses[i]) {
                        biggest_witness = witnesses[i];
			biggest_witness_idx = i;
			biggest_witness_cnt = 1;
			continue;
		}

		if (biggest_witness == witnesses[i])
			biggest_witness_cnt++;
        }

	if (biggest_witness_cnt != 1)
		return -1;

        return biggest_witness_idx;
}

int
afr_mark_fool_as_source_by_witness (int32_t *sources, int32_t *witnesses,
                                    afr_node_character *characters,
                                    int32_t child_count, int32_t witness)
{
        int i        = 0;
        int nsources = 0;

        GF_ASSERT (sources);
        GF_ASSERT (witnesses);
        GF_ASSERT (characters);
        GF_ASSERT (child_count > 0);

        for (i = 0; i < child_count; i++) {
                if (characters[i].type != AFR_NODE_FOOL)
                        continue;

                if (witness == witnesses[i]) {
                        sources[i] = 1;
                        nsources++;
                }
        }
        return nsources;
}


int
afr_mark_fool_as_source_by_idx (int32_t *sources, int child_count, int idx)
{
	if (idx >= 0 && idx < child_count) {
		sources[idx] = 1;
		return 1;
	}
	return 0;
}


static int
afr_find_largest_file_size (struct iatt *bufs, int32_t *success_children,
			    int child_count)
{
	int idx = -1;
	int i = -1;
	int child = -1;
	uint64_t max_size = 0;
        uint64_t min_size = 0;
        int      num_children = 0;

	for (i = 0; i < child_count; i++) {
		if (success_children[i] == -1)
			break;

		child = success_children[i];
		if (bufs[child].ia_size > max_size) {
			max_size = bufs[child].ia_size;
			idx = child;
		}

                if ((num_children == 0) || (bufs[child].ia_size < min_size)) {
                        min_size = bufs[child].ia_size;
                }

                num_children++;
	}

        /* If sizes are same for all of them, finding sources will have to
         * happen with pending changelog. So return -1
         */
        if ((num_children > 1) && (min_size == max_size))
                return -1;
	return idx;
}


static int
afr_find_newest_file (struct iatt *bufs, int32_t *success_children,
		      int child_count)
{
	int idx = -1;
	int i = -1;
	int child = -1;
	uint64_t max_ctime = 0;

	for (i = 0; i < child_count; i++) {
		if (success_children[i] == -1)
			break;

		child = success_children[i];
		if (bufs[child].ia_ctime > max_ctime) {
			max_ctime = bufs[child].ia_ctime;
			idx = child;
		}
	}

	return idx;
}


static int
afr_mark_biggest_of_fools_as_source (int32_t *sources, int32_t **pending_matrix,
                                     afr_node_character *characters,
				     int32_t *success_children,
                                     int child_count, struct iatt *bufs)
{
        int32_t       biggest_witness = 0;
        int           nsources        = 0;
        int32_t       *witnesses      = NULL;

        GF_ASSERT (child_count > 0);

	biggest_witness = afr_find_largest_file_size (bufs, success_children,
						      child_count);
	if (biggest_witness != -1)
		goto found;

        witnesses = GF_CALLOC (child_count, sizeof (*witnesses),
                               gf_afr_mt_int32_t);
        if (NULL == witnesses) {
                nsources = -1;
                goto out;
        }

        afr_compute_witness_of_fools (witnesses, pending_matrix, characters,
                                      child_count);
        biggest_witness = afr_find_biggest_witness_among_fools (witnesses,
                                                                characters,
                                                                child_count);
	if (biggest_witness != -1)
		goto found;

	biggest_witness = afr_find_newest_file (bufs, success_children,
						child_count);

found:
	nsources = afr_mark_fool_as_source_by_idx (sources, child_count,
						   biggest_witness);
out:
        GF_FREE (witnesses);
        return nsources;
}

int
afr_mark_child_as_source_by_uid (int32_t *sources, struct iatt *bufs,
                                 int32_t *success_children,
                                 unsigned int child_count, uint32_t uid)
{
        int     i        = 0;
        int     nsources = 0;
        int     child    = 0;

        for (i = 0; i < child_count; i++) {
                if (-1 == success_children[i])
                        break;

                child = success_children[i];
                if (uid == bufs[child].ia_uid) {
                        sources[child] = 1;
                        nsources++;
                }
        }
        return nsources;
}

int
afr_get_child_with_lowest_uid (struct iatt *bufs, int32_t *success_children,
                               unsigned int child_count)
{
        int     i        = 0;
        int     smallest = -1;
        int     child    = 0;

        for (i = 0; i < child_count; i++) {
                if (-1 == success_children[i])
                        break;
                child = success_children[i];
                if ((smallest == -1) ||
                    (bufs[child].ia_uid < bufs[smallest].ia_uid)) {
                        smallest = child;
                }
        }
        return smallest;
}

static int
afr_sh_mark_lowest_uid_as_source (struct iatt *bufs, int32_t *success_children,
                                  int child_count, int32_t *sources)
{
        int   nsources              = 0;
        int   smallest              = 0;

        smallest = afr_get_child_with_lowest_uid (bufs, success_children,
                                                  child_count);
        if (smallest < 0) {
                nsources = -1;
                goto out;
        }
        nsources = afr_mark_child_as_source_by_uid (sources, bufs,
                                                    success_children, child_count,
                                                    bufs[smallest].ia_uid);
out:
        return nsources;
}

int
afr_get_no_xattr_dir_read_child (xlator_t *this, int32_t *success_children,
                                 struct iatt *bufs)
{
        afr_private_t *priv = NULL;
        int            i = 0;
        int            child = -1;
        int            read_child = -1;

        priv = this->private;
        for (i = 0; i < priv->child_count; i++) {
                child = success_children[i];
                if (child < 0)
                        break;
                if (read_child < 0)
                        read_child = child;
                else if (bufs[read_child].ia_size < bufs[child].ia_size)
                        read_child = child;
        }
        return read_child;
}

int
afr_sh_mark_zero_size_file_as_sink (struct iatt *bufs, int32_t *success_children,
                                    int child_count, int32_t *sources)
{
        int             nsources = 0;
        int             i = 0;
        int             child = 0;
        gf_boolean_t    sink_exists = _gf_false;
        gf_boolean_t    source_exists = _gf_false;
        int             source = -1;

        for (i = 0; i < child_count; i++) {
                child = success_children[i];
                if (child < 0)
                        break;
                if (!bufs[child].ia_size) {
                        sink_exists = _gf_true;
                        continue;
                }
                if (!source_exists) {
                        source_exists = _gf_true;
                        source = child;
                        continue;
                }
                if (bufs[source].ia_size != bufs[child].ia_size) {
                        nsources = -1;
                        goto out;
                }
        }
        if (!source_exists && !sink_exists) {
                nsources = -1;
                goto out;
        }

        if (!source_exists || !sink_exists)
                goto out;

        for (i = 0; i < child_count; i++) {
                child = success_children[i];
                if (child < 0)
                        break;
                if (bufs[child].ia_size) {
                        sources[child] = 1;
                        nsources++;
                }
        }
out:
        return nsources;
}

char *
afr_get_character_str (afr_node_type type)
{
        char *character = NULL;

        switch (type) {
        case AFR_NODE_INNOCENT:
                character = "innocent";
                break;
        case AFR_NODE_FOOL:
                character = "fool";
                break;
        case AFR_NODE_WISE:
                character = "wise";
                break;
        default:
                character = "invalid";
                break;
        }
        return character;
}

afr_node_type
afr_find_child_character_type (int32_t *pending_row, int32_t child,
                               unsigned int child_count)
{
        afr_node_type type = AFR_NODE_INVALID;

        GF_ASSERT ((child >= 0) && (child < child_count));

        if (afr_sh_is_innocent (pending_row, child_count))
                type = AFR_NODE_INNOCENT;
        else if (afr_sh_is_fool (pending_row, child, child_count))
                type = AFR_NODE_FOOL;
        else if (afr_sh_is_wise (pending_row, child, child_count))
                type = AFR_NODE_WISE;
        return type;
}

int
afr_build_sources (xlator_t *this, dict_t **xattr, struct iatt *bufs,
                   int32_t **pending_matrix, int32_t *sources,
                   int32_t *success_children, afr_transaction_type type,
                   int32_t *subvol_status, gf_boolean_t ignore_ignorant)
{
        afr_private_t           *priv = NULL;
        afr_self_heal_type      sh_type    = AFR_SELF_HEAL_INVALID;
        int                     nsources   = -1;
        unsigned char           *ignorant_subvols = NULL;
        unsigned int            child_count = 0;

        priv = this->private;
        child_count = priv->child_count;

        if (afr_get_children_count (success_children, priv->child_count) == 0)
                goto out;

        if (!ignore_ignorant) {
                ignorant_subvols = GF_CALLOC (sizeof (*ignorant_subvols),
                                              child_count, gf_afr_mt_char);
                if (NULL == ignorant_subvols)
                        goto out;
        }

        afr_build_pending_matrix (priv->pending_key, pending_matrix,
                                  ignorant_subvols, xattr, type,
                                  priv->child_count);

        if (!ignore_ignorant)
                afr_mark_ignorant_subvols_as_pending (pending_matrix,
                                                      ignorant_subvols,
                                                      priv->child_count);
        sh_type = afr_self_heal_type_for_transaction (type);
        if (AFR_SELF_HEAL_INVALID == sh_type)
                goto out;

        afr_sh_print_pending_matrix (pending_matrix, this);

        nsources = afr_mark_sources (this, sources, pending_matrix, bufs,
                                     sh_type, success_children, subvol_status);
out:
        GF_FREE (ignorant_subvols);
        return nsources;
}

void
afr_find_character_types (afr_node_character *characters,
                          int32_t **pending_matrix, int32_t *success_children,
                          unsigned int child_count)
{
        afr_node_type type  = AFR_NODE_INVALID;
        int           child = 0;
        int           i     = 0;

        for (i = 0; i < child_count; i++) {
                child = success_children[i];
                if (child == -1)
                        break;
                type = afr_find_child_character_type (pending_matrix[child],
                                                      child, child_count);
                characters[child].type = type;
        }
}

void
afr_mark_success_children_sources (int32_t *sources, int32_t *success_children,
                                   unsigned int child_count)
{
        int i = 0;
        for (i = 0; i < child_count; i++) {
                if (success_children[i] == -1)
                        break;
                sources[success_children[i]] = 1;
        }
}

/**
 * mark_sources: Mark all 'source' nodes and return number of source
 * nodes found
 *
 * A node (a row in the pending matrix) belongs to one of
 * three categories:
 *
 * M is the pending matrix.
 *
 * 'innocent' - M[i] is all zeroes
 * 'fool'     - M[i] has i'th element = 1 (self-reference)
 * 'wise'     - M[i] has i'th element = 0, others are 1 or 0.
 *
 * All 'innocent' nodes are sinks. If all nodes are innocent, no self-heal is
 * needed.
 *
 * A 'wise' node can be a source. If two 'wise' nodes conflict, it is
 * a split-brain. If one wise node refers to the other but the other doesn't
 * refer back, the referrer is a source.
 *
 * All fools are sinks, unless there are no 'wise' nodes. In that case,
 * one of the fools is made a source.
 */

int
afr_mark_sources (xlator_t *this, int32_t *sources, int32_t **pending_matrix,
                  struct iatt *bufs, afr_self_heal_type type,
                  int32_t *success_children, int32_t *subvol_status)
{
        /* stores the 'characters' (innocent, fool, wise) of the nodes */
        afr_node_character *characters =  NULL;
        int                nsources    = -1;
        unsigned int       child_count = 0;
        afr_private_t      *priv       = NULL;

        priv = this->private;
        child_count = priv->child_count;
        characters = GF_CALLOC (sizeof (afr_node_character),
                                child_count, gf_afr_mt_afr_node_character);
        if (!characters)
                goto out;

        this = THIS;

        /* start clean */
        memset (sources, 0, sizeof (*sources) * child_count);
        nsources = 0;
        afr_find_character_types (characters, pending_matrix, success_children,
                                  child_count);

        if ((type == AFR_SELF_HEAL_ENTRY) &&
            afr_sh_has_any_fools (characters, child_count)) {
                /*
                 * Force a conservative merge even if there is a single FOOL
                 * in the characters. Just because a brick's state is FOOL we
                 * should not assume that the brick does not have new entries.
                 * Example where this matters:
                 *All the operations done in this directory are 'creates'.
                 0) create files a, b in the directory.
                 1) bring down brick-0 (either the process or network) and
                    create file 'c'. -- This makes brick-1 as 'source'.
                 2) Disable self-heal-daemon and entry-self-heal to simulate
                    the case where self-heal for this directory is not happened
                    yet.
                 3) bring up brick-0 and bring down brick-1.
                 4) create files d, e and before 'post-op' can complete for the
                    transaction for creation of 'e' bring down brick-0.  That
                    leaves brick-0 in 'FOOL' state. If the post-op had
                    completed for brick-0, it would also have gone to 'source'
                    state and there would have been a split-brain where
                    conservative merge would have happened and all the files
                    would have been there.  But because post-op is not
                    complete, changelogs say brick-1 is correct and brick-0 is
                    stale which leads to removal of files d, e from brick-0.
                 */
                if (subvol_status)
                        *subvol_status |= SPLIT_BRAIN;
                //nsources is set to -1 when there is a split-brain
                nsources = -1;
                goto out;
        }

        if (afr_sh_all_nodes_innocent (characters, child_count)) {
                switch (type) {
                case AFR_SELF_HEAL_METADATA:
                        nsources = afr_sh_mark_lowest_uid_as_source (bufs,
                                                             success_children,
                                                             child_count,
                                                             sources);
                        break;
                case AFR_SELF_HEAL_DATA:
                        nsources = afr_sh_mark_zero_size_file_as_sink (bufs,
                                                             success_children,
                                                             child_count,
                                                             sources);
                        if ((nsources < 0) && subvol_status)
                                *subvol_status |= SPLIT_BRAIN;
                        break;
                default:
                        break;
                }
                goto out;
        }

        if (afr_sh_wise_nodes_exist (characters, child_count)) {
                afr_sh_compute_wisdom (pending_matrix, characters, child_count);

                if (afr_sh_wise_nodes_conflict (characters, child_count)) {
                        if (subvol_status)
                                *subvol_status |= SPLIT_BRAIN;
                        nsources = -1;
                } else {
                        nsources = afr_sh_mark_wisest_as_sources (sources,
                                                                  characters,
                                                                  child_count);
                }
        } else {
                if (subvol_status)
                        *subvol_status |= ALL_FOOLS;
                nsources = afr_mark_biggest_of_fools_as_source (sources,
                                                                pending_matrix,
                                                                characters,
								success_children,
                                                                child_count, bufs);
        }

out:
        if (nsources == 0)
                afr_mark_success_children_sources (sources, success_children,
                                                   child_count);
        GF_FREE (characters);

        gf_log (this->name, GF_LOG_DEBUG, "Number of sources: %d", nsources);
        return nsources;
}

void
afr_sh_pending_to_delta (afr_private_t *priv, dict_t **xattr,
                         int32_t *delta_matrix[], unsigned char success[],
                         int child_count, afr_transaction_type type)
{
        int     tgt     = 0;
        int     src     = 0;
        int     value   = 0;

        afr_build_pending_matrix (priv->pending_key, delta_matrix, NULL,
                                  xattr, type, priv->child_count);

        /*
         * The algorithm here has two parts.  First, for each subvol indexed
         * as tgt, we try to figure out what count everyone should have for it.
         * If the self-heal succeeded, that's easy; the value is zero.
         * Otherwise, the value is the maximum of the succeeding nodes' counts.
         * Once we know the value, we loop through (possibly for a second time)
         * setting each count to the difference so that when we're done all
         * succeeding nodes will have the same count for tgt.
         */
        for (tgt = 0; tgt < priv->child_count; ++tgt) {
                value = 0;
                if (!success[tgt]) {
                        /* Find the maximum. */
                        for (src = 0; src < priv->child_count; ++src) {
                                if (!success[src]) {
                                        continue;
                                }
                                if (delta_matrix[src][tgt] > value) {
                                        value = delta_matrix[src][tgt];
                                }
                        }
                }
                /* Force everyone who succeeded to the chosen value. */
                for (src = 0; src < priv->child_count; ++src) {
                        if (success[src]) {
                                delta_matrix[src][tgt] = value
                                                       - delta_matrix[src][tgt];
                        }
                        else {
                                delta_matrix[src][tgt] = 0;
                        }
                }
        }
}


int
afr_sh_delta_to_xattr (xlator_t *this,
                       int32_t *delta_matrix[], dict_t *xattr[],
                       int child_count, afr_transaction_type type)
{
        int              i       = 0;
        int              j       = 0;
        int              k       = 0;
        int              ret     = 0;
        int32_t         *pending = NULL;
        int32_t         *local_pending = NULL;
        afr_private_t   *priv = NULL;

        priv = this->private;
        for (i = 0; i < child_count; i++) {
                if (!xattr[i])
                        continue;

                local_pending = NULL;
                for (j = 0; j < child_count; j++) {
                        pending = GF_CALLOC (sizeof (int32_t), 3,
                                             gf_afr_mt_int32_t);

                        if (!pending) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to allocate pending entry "
                                        "for %s[%d] on %s",
                                        priv->pending_key[j], type,
                                        priv->children[i]->name);
                                continue;
                        }
                        /* 3 = data+metadata+entry */

                        k = afr_index_for_transaction_type (type);

                        pending[k] = hton32 (delta_matrix[i][j]);

                        if (j == i) {
                                local_pending = pending;
                                continue;
                        }
                        ret = dict_set_bin (xattr[i], priv->pending_key[j],
                                            pending,
                                        AFR_NUM_CHANGE_LOGS * sizeof (int32_t));
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Unable to set dict value.");
                                GF_FREE (pending);
                        }
                }
                if (local_pending) {
                        ret = dict_set_bin (xattr[i], priv->pending_key[i],
                                            local_pending,
                                        AFR_NUM_CHANGE_LOGS * sizeof (int32_t));
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Unable to set dict value.");
                                GF_FREE (local_pending);
                        }
                }
        }
        return 0;
}


int
afr_sh_missing_entries_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        afr_sh_reset (frame, this);

        if (local->unhealable) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "split brain found, aborting selfheal of %s",
                        local->loc.path);
        }

        if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
                sh->completion_cbk (frame, this);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "proceeding to metadata check on %s",
                        local->loc.path);
                afr_self_heal_metadata (frame, this);
        }

        return 0;
}


static int
afr_sh_missing_entries_finish (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local = frame->local;
        int_lock = &local->internal_lock;

        int_lock->lock_cbk = afr_sh_missing_entries_done;
        afr_unlock (frame, this);

        return 0;
}

int
afr_sh_common_create (afr_self_heal_t *sh, unsigned int child_count)
{
        int     ret = -ENOMEM;
        sh->buf = GF_CALLOC (child_count, sizeof (*sh->buf),
                             gf_afr_mt_iatt);
        if (!sh->buf)
                goto out;
        sh->parentbufs = GF_CALLOC (child_count, sizeof (*sh->parentbufs),
                                    gf_afr_mt_iatt);
        if (!sh->parentbufs)
                goto out;
        sh->child_errno = GF_CALLOC (child_count, sizeof (*sh->child_errno),
                                     gf_afr_mt_int);
        if (!sh->child_errno)
                goto out;
        sh->success_children = afr_children_create (child_count);
        if (!sh->success_children)
                goto out;
        sh->fresh_children = afr_children_create (child_count);
        if (!sh->fresh_children)
                goto out;
        sh->xattr = GF_CALLOC (child_count, sizeof (*sh->xattr),
                               gf_afr_mt_dict_t);
        if (!sh->xattr)
                goto out;
        ret = 0;
out:
        return ret;
}

void
afr_sh_common_lookup_resp_handler (call_frame_t *frame, void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret, int32_t op_errno,
                                   inode_t *inode, struct iatt *buf,
                                   dict_t *xattr, struct iatt *postparent,
                                   loc_t *loc)
{
        int              child_index = 0;
        afr_local_t     *local = NULL;
        afr_private_t   *priv = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        priv = this->private;
        sh   = &local->self_heal;
        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        sh->buf[child_index] = *buf;
                        sh->parentbufs[child_index] = *postparent;
                        sh->success_children[sh->success_count] = child_index;
                        sh->success_count++;
                        sh->xattr[child_index] = dict_ref (xattr);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG, "path %s on subvolume"
                                " %s => -1 (%s)", loc->path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        local->self_heal.child_errno[child_index] = op_errno;
                }
        }
        UNLOCK (&frame->lock);
        return;
}

gf_boolean_t
afr_valid_ia_type (ia_type_t ia_type)
{
        switch (ia_type) {
        case IA_IFSOCK:
        case IA_IFREG:
        case IA_IFBLK:
        case IA_IFCHR:
        case IA_IFIFO:
        case IA_IFLNK:
        case IA_IFDIR:
                return _gf_true;
        default:
                return _gf_false;
        }
        return _gf_false;
}

int
afr_impunge_frame_create (call_frame_t *frame, xlator_t *this,
                          int active_source, call_frame_t **impunge_frame)
{
        afr_local_t     *local         = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh    = NULL;
        int32_t         op_errno       = 0;
        afr_private_t   *priv          = NULL;
        int             ret            = 0;
        call_frame_t    *new_frame     = NULL;

        op_errno = ENOMEM;
        priv = this->private;
        new_frame = copy_frame (frame);
        if (!new_frame) {
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (impunge_local, out);

        local = frame->local;
        new_frame->local = impunge_local;
        impunge_sh = &impunge_local->self_heal;
        impunge_sh->sh_frame = frame;
        impunge_sh->active_source = active_source;
        impunge_local->child_up  = memdup (local->child_up,
                                           sizeof (*local->child_up) *
                                           priv->child_count);
        if (!impunge_local->child_up)
                goto out;

        impunge_local->pending = afr_matrix_create (priv->child_count,
                                                    AFR_NUM_CHANGE_LOGS);
        if (!impunge_local->pending)
                goto out;

        ret = afr_sh_common_create (impunge_sh, priv->child_count);
        if (ret) {
                op_errno = -ret;
                goto out;
        }
        op_errno = 0;
        *impunge_frame = new_frame;
out:
        if (op_errno && new_frame)
                AFR_STACK_DESTROY (new_frame);
        return -op_errno;
}

void
afr_sh_missing_entry_call_impunge_recreate (call_frame_t *frame, xlator_t *this,
                                            struct iatt *buf,
                                            struct iatt *postparent,
                                            afr_impunge_done_cbk_t impunge_done)
{
        call_frame_t    *impunge_frame = NULL;
        afr_local_t     *local = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int             ret = 0;
        unsigned int    enoent_count = 0;
        afr_private_t   *priv = NULL;
        int             i = 0;
        int32_t         op_errno = 0;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        enoent_count = afr_errno_count (NULL, sh->child_errno,
                                        priv->child_count, ENOENT);
        if (!enoent_count) {
                gf_log (this->name, GF_LOG_INFO,
                        "no missing files - %s. proceeding to metadata check",
                        local->loc.path);
                goto out;
        }
        sh->impunge_done = impunge_done;
        ret = afr_impunge_frame_create (frame, this, sh->source, &impunge_frame);
        if (ret)
                goto out;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;
        loc_copy (&impunge_local->loc, &local->loc);
        ret = afr_build_parent_loc (&impunge_sh->parent_loc,
                                    &impunge_local->loc, &op_errno);
        if (ret) {
                ret = -op_errno;
                goto out;
        }
        impunge_local->call_count = enoent_count;
        impunge_sh->entrybuf = sh->buf[sh->source];
        impunge_sh->parentbuf = sh->parentbufs[sh->source];
        for (i = 0; i < priv->child_count; i++) {
                if (!impunge_local->child_up[i]) {
                        impunge_sh->child_errno[i] = ENOTCONN;
                        continue;
                }
                if (sh->child_errno[i] != ENOENT) {
                        impunge_sh->child_errno[i] = EEXIST;
                        continue;
                }
        }
        for (i = 0; i < priv->child_count; i++) {
                if (sh->child_errno[i] != ENOENT)
                        continue;
                afr_sh_entry_impunge_create (impunge_frame, this, i);
                enoent_count--;
        }
        GF_ASSERT (!enoent_count);
        return;
out:
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "impunge of %s failed, "
                        "reason: %s", local->loc.path, strerror (-ret));
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
        }
        afr_sh_missing_entries_finish (frame, this);
}

int
afr_sh_create_entry_cbk (call_frame_t *frame, xlator_t *this,
                         int32_t op_ret, int32_t op_errno)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        if (op_ret < 0)
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
        afr_sh_missing_entries_finish (frame, this);
        return 0;
}

static int
sh_missing_entries_create (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int              type = 0;
        struct iatt     *buf = NULL;
        struct iatt     *postparent = NULL;

        local = frame->local;
        sh = &local->self_heal;

        buf = &sh->buf[sh->source];
        postparent = &sh->parentbufs[sh->source];

        type = buf->ia_type;
        if (!afr_valid_ia_type (type)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: unknown file type: 0%o", local->loc.path, type);
                afr_set_local_for_unhealable (local);
                afr_sh_missing_entries_finish (frame, this);
                goto out;
        }

        afr_sh_missing_entry_call_impunge_recreate (frame, this,
                                                    buf, postparent,
                                                    afr_sh_create_entry_cbk);
out:
        return 0;
}

void
afr_sh_missing_entries_lookup_done (call_frame_t *frame, xlator_t *this,
                                    int32_t op_ret, int32_t op_errno)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        ia_type_t       ia_type = IA_INVAL;
        int32_t         nsources = 0;
        loc_t           *loc = NULL;
        int32_t         subvol_status = 0;
        afr_transaction_type txn_type = AFR_DATA_TRANSACTION;
        gf_boolean_t    split_brain = _gf_false;
        int             read_child = -1;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;
        loc = &local->loc;

        if (op_ret < 0) {
                if (op_errno == EIO) {
                        afr_set_local_for_unhealable (local);
                }
                // EIO can happen if finding the fresh parent dir failed
                goto out;
        }

        //now No chance for the ia_type to conflict
        ia_type = sh->buf[sh->success_children[0]].ia_type;
        txn_type = afr_transaction_type_get (ia_type);
        nsources = afr_build_sources (this, sh->xattr, sh->buf,
                                      sh->pending_matrix, sh->sources,
                                      sh->success_children, txn_type,
                                      &subvol_status, _gf_false);
        if (nsources < 0) {
                gf_log (this->name, GF_LOG_INFO, "No sources for dir of %s,"
                        " in missing entry self-heal, continuing with the rest"
                        " of the self-heals", local->loc.path);
                if (subvol_status & SPLIT_BRAIN) {
                        split_brain = _gf_true;
                        switch (txn_type) {
                        case AFR_DATA_TRANSACTION:
                                nsources = 1;
                                sh->sources[sh->success_children[0]] = 1;
                                break;
                        case AFR_ENTRY_TRANSACTION:
                                read_child = afr_get_no_xattr_dir_read_child
                                                          (this,
                                                           sh->success_children,
                                                           sh->buf);
                                sh->sources[read_child] = 1;
                                nsources = 1;
                                break;
                        default:
                                op_errno = EIO;
                                goto out;
                        }
                } else {
                        op_errno = EIO;
                        goto out;
                }
        }

        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_children, priv->child_count);
        sh->source = sh->fresh_children[0];
        if (sh->source == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "No active sources found.");
                op_errno = EIO;
                goto out;
        }

        if (sh->gfid_sh_success_cbk)
                sh->gfid_sh_success_cbk (frame, this);
        sh->type = sh->buf[sh->source].ia_type;
        if (uuid_is_null (loc->inode->gfid))
                uuid_copy (loc->gfid, sh->buf[sh->source].ia_gfid);
        if (split_brain) {
                afr_sh_missing_entries_finish (frame, this);
        } else {
                sh_missing_entries_create (frame, this);
        }
        return;
out:
        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
        afr_sh_set_error (sh, op_errno);
        afr_sh_missing_entries_finish (frame, this);
        return;
}

static int
afr_sh_common_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *buf, dict_t *xattr,
                          struct iatt *postparent)
{
        int                     call_count = 0;
        afr_local_t             *local = NULL;
        afr_self_heal_t         *sh    = NULL;
        afr_private_t           *priv  = NULL;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        afr_sh_common_lookup_resp_handler (frame, cookie, this, op_ret,
                                           op_errno, inode, buf, xattr,
                                           postparent, &sh->lookup_loc);
        call_count = afr_frame_return (frame);

        if (call_count)
                goto out;
        op_ret = -1;
        if (!sh->success_count) {
                op_errno = afr_resultant_errno_get (NULL, sh->child_errno,
                                                    priv->child_count);
                gf_log (this->name, GF_LOG_ERROR, "Failed to lookup %s, "
                        "reason %s", sh->lookup_loc.path,
                        strerror (op_errno));
                goto done;
        }

        if ((sh->lookup_flags & AFR_LOOKUP_FAIL_CONFLICTS) &&
            (afr_conflicting_iattrs (sh->buf, sh->success_children,
                                     priv->child_count,
                                     sh->lookup_loc.path, this->name))) {
                op_errno = EIO;
                gf_log (this->name, GF_LOG_ERROR, "Conflicting entries "
                        "for %s", sh->lookup_loc.path);
                goto done;
        }

        if ((sh->lookup_flags & AFR_LOOKUP_FAIL_MISSING_GFIDS) &&
            (afr_gfid_missing_count (this->name, sh->success_children,
                                     sh->buf, priv->child_count,
                                     sh->lookup_loc.path))) {
                op_errno = ENODATA;
                gf_log (this->name, GF_LOG_ERROR, "Missing Gfids "
                        "for %s", sh->lookup_loc.path);
                goto done;
        }
        op_ret = 0;

done:
        sh->lookup_done (frame, this, op_ret, op_errno);
out:
        return 0;
}

int
afr_sh_remove_entry_cbk (call_frame_t *frame, xlator_t *this, int child,
                         int32_t op_ret, int32_t op_errno)
{
        int             call_count = 0;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->post_remove_call);
        if ((op_ret == -1) && (op_errno != ENOENT)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "purge entry %s failed, on child %d reason, %s",
                        local->loc.path, child, strerror (op_errno));
                LOCK (&frame->lock);
                {
                        afr_sh_set_error (sh, EIO);
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                }
                UNLOCK (&frame->lock);
        }
        call_count = afr_frame_return (frame);
        if (call_count == 0)
                sh->post_remove_call (frame, this);
        return 0;
}

void
afr_sh_call_entry_expunge_remove (call_frame_t *frame, xlator_t *this,
                                  int child_index, struct iatt *buf,
                                  struct iatt *parentbuf,
                                  afr_expunge_done_cbk_t expunge_done)
{
        call_frame_t    *expunge_frame = NULL;
        afr_local_t     *local = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        int32_t         op_errno = 0;
        int             ret = 0;

        expunge_frame = copy_frame (frame);
        if (!expunge_frame) {
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (expunge_local, out);

        local = frame->local;
        sh = &local->self_heal;
        expunge_frame->local = expunge_local;
        expunge_sh = &expunge_local->self_heal;
        expunge_sh->sh_frame = frame;
        loc_copy (&expunge_local->loc, &local->loc);
        ret = afr_build_parent_loc (&expunge_sh->parent_loc,
                                    &expunge_local->loc, &op_errno);
        if (ret) {
                ret = -op_errno;
                goto out;
        }
        sh->expunge_done = expunge_done;
        afr_sh_entry_expunge_remove (expunge_frame, this, child_index, buf,
                                     parentbuf);
        return;
out:
        gf_log (this->name, GF_LOG_ERROR, "Expunge of %s failed, reason: %s",
                local->loc.path, strerror (op_errno));
        expunge_done (frame, this, child_index, -1, op_errno);
}

void
afr_sh_remove_stale_lookup_info (afr_self_heal_t *sh, int32_t *success_children,
                                 int32_t *fresh_children,
                                 unsigned int child_count)
{
        int     i = 0;

        for (i = 0; i < child_count; i++) {
                if (afr_is_child_present (success_children, child_count, i) &&
                    !afr_is_child_present (fresh_children, child_count, i)) {
                        sh->child_errno[i] = ENOENT;
                        GF_ASSERT (sh->xattr[i]);
                        dict_unref (sh->xattr[i]);
                        sh->xattr[i] = NULL;
                }
        }
}

int
afr_sh_purge_stale_entries_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;
        afr_private_t       *priv     = NULL;

        local    = frame->local;
        sh       = &local->self_heal;
        priv     = this->private;

        if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
                afr_sh_missing_entries_finish (frame, this);
        } else {
                if (afr_gfid_missing_count (this->name, sh->fresh_children,
                                            sh->buf, priv->child_count,
                                            local->loc.path)) {
                        afr_sh_common_lookup (frame, this, &local->loc,
                                              afr_sh_missing_entries_lookup_done,
                                              sh->sh_gfid_req,
                                              AFR_LOOKUP_FAIL_CONFLICTS|
                                              AFR_LOOKUP_FAIL_MISSING_GFIDS,
                                              NULL);
                } else {
                        //No need to set gfid so goto missing entries lookup done
                        //Behave as if you have done the lookup
                        afr_sh_remove_stale_lookup_info (sh,
                                                         sh->success_children,
                                                         sh->fresh_children,
                                                         priv->child_count);
                        afr_children_copy (sh->success_children,
                                           sh->fresh_children,
                                           priv->child_count);
                        afr_sh_missing_entries_lookup_done (frame, this, 0, 0);
                }
        }
        return 0;
}

gf_boolean_t
afr_sh_purge_entry_condition (afr_local_t *local, afr_private_t *priv,
                              int child)
{
        afr_self_heal_t *sh = NULL;

        sh = &local->self_heal;

        if (local->child_up[child] &&
            (!afr_is_child_present (sh->fresh_parent_dirs, priv->child_count,
                                    child))
            && (sh->child_errno[child] != ENOENT))
                return _gf_true;

        return _gf_false;
}

gf_boolean_t
afr_sh_purge_stale_entry_condition (afr_local_t *local, afr_private_t *priv,
                                    int child)
{
        afr_self_heal_t *sh = NULL;

        sh = &local->self_heal;

        if (local->child_up[child] &&
            (!afr_is_child_present (sh->fresh_children, priv->child_count,
                                    child))
             && (sh->child_errno[child] != ENOENT))
                return _gf_true;

        return _gf_false;
}

void
afr_sh_purge_entry_common (call_frame_t *frame, xlator_t *this,
                           gf_boolean_t purge_condition (afr_local_t *local,
                                                         afr_private_t *priv,
                                                         int child))
{
        afr_local_t     *local = NULL;
        afr_private_t   *priv = NULL;
        afr_self_heal_t *sh = NULL;
        int             i = 0;
        int             call_count = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (purge_condition (local, priv, i))
                        call_count++;
        }

        if (call_count == 0) {
                sh->post_remove_call (frame, this);
                goto out;
        }

        local->call_count = call_count;
        for (i = 0; i < priv->child_count; i++) {
                if (!purge_condition (local, priv, i))
                        continue;
                gf_log (this->name, GF_LOG_INFO, "purging the stale entry %s "
                        "on %s", local->loc.path, priv->children[i]->name);
                afr_sh_call_entry_expunge_remove (frame, this,
                                                  (long) i, &sh->buf[i],
                                                  &sh->parentbufs[i],
                                                  afr_sh_remove_entry_cbk);
        }
out:
        return;
}

void
afr_sh_purge_entry (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        sh->post_remove_call = afr_sh_missing_entries_finish;

        afr_sh_purge_entry_common (frame, this, afr_sh_purge_entry_condition);
}

void
afr_sh_purge_stale_entry (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int             i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        sh->post_remove_call = afr_sh_purge_stale_entries_done;

        for (i = 0; i < priv->child_count; i++) {
                if (afr_is_child_present (sh->fresh_children,
                                          priv->child_count, i))
                        continue;

                if ((!local->child_up[i]) || sh->child_errno[i] != 0)
                        continue;

                GF_ASSERT (!uuid_is_null (sh->entrybuf.ia_gfid) ||
                           uuid_is_null (sh->buf[i].ia_gfid));

                if ((sh->entrybuf.ia_type != sh->buf[i].ia_type) ||
                    (uuid_compare (sh->buf[i].ia_gfid,
                                   sh->entrybuf.ia_gfid)))
                        continue;

                afr_children_add_child (sh->fresh_children, i,
                                        priv->child_count);

        }
        afr_sh_purge_entry_common (frame, this,
                                   afr_sh_purge_stale_entry_condition);
}

void
afr_sh_save_child_iatts_from_policy (int32_t *children, struct iatt *bufs,
                                     struct iatt *save,
                                     unsigned int child_count)
{
        int             i = 0;
        int             child = 0;
        gf_boolean_t    saved = _gf_false;

        GF_ASSERT (save);
        //if iatt buf with gfid exists sets it
        for (i = 0; i < child_count; i++) {
                child = children[i];
                if (child == -1)
                        break;
                *save = bufs[child];
                saved = _gf_true;
                if (!uuid_is_null (save->ia_gfid))
                        break;
        }
        GF_ASSERT (saved);
}

void
afr_get_children_of_fresh_parent_dirs (afr_self_heal_t *sh,
                                       unsigned int child_count)
{
        afr_children_intersection_get (sh->success_children,
                                       sh->fresh_parent_dirs,
                                       sh->sources, child_count);
        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_children, child_count);
        memset (sh->sources, 0, sizeof (*sh->sources) * child_count);
}

void
afr_sh_children_lookup_done (call_frame_t *frame, xlator_t *this,
                             int32_t op_ret, int32_t op_errno)
{
        afr_local_t      *local = NULL;
        afr_self_heal_t  *sh = NULL;
        afr_private_t    *priv = NULL;
        int32_t          fresh_child_enoents = 0;
        int32_t          fresh_parent_count = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        if (op_ret < 0)
                goto fail;
        afr_get_children_of_fresh_parent_dirs (sh, priv->child_count);
        fresh_parent_count = afr_get_children_count (sh->fresh_parent_dirs,
                                                     priv->child_count);
        //we need the enoent count of the subvols present in fresh_parent_dirs
        fresh_child_enoents = afr_errno_count (sh->fresh_parent_dirs,
                                               sh->child_errno,
                                               priv->child_count, ENOENT);
        if (fresh_child_enoents == fresh_parent_count) {
                afr_sh_set_error (sh, ENOENT);
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                afr_sh_purge_entry (frame, this);
        } else if (!afr_conflicting_iattrs (sh->buf, sh->fresh_children,
                                            priv->child_count, local->loc.path,
                                            this->name)) {
                afr_sh_save_child_iatts_from_policy (sh->fresh_children,
                                                     sh->buf, &sh->entrybuf,
                                                     priv->child_count);
                afr_update_gfid_from_iatts (sh->sh_gfid_req, sh->buf,
                                            sh->fresh_children,
                                            priv->child_count);
                afr_sh_purge_stale_entry (frame, this);
        } else {
                op_errno = EIO;
                afr_set_local_for_unhealable (local);
                goto fail;
        }

        return;

fail:
        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
        afr_sh_set_error (sh, op_errno);
        afr_sh_missing_entries_finish (frame, this);
        return;
}

static void
afr_sh_find_fresh_parents (call_frame_t *frame, xlator_t *this,
                           int32_t op_ret, int32_t op_errno)
{
        afr_self_heal_t *sh  = NULL;
        afr_private_t   *priv = NULL;
        afr_local_t     *local = NULL;
        int             enoent_count = 0;
        int             nsources = 0;
        int             source  = -1;
        int32_t         subvol_status = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        if (op_ret < 0)
                goto out;
        enoent_count = afr_errno_count (NULL, sh->child_errno,
                                        priv->child_count, ENOENT);
        if (enoent_count > 0) {
                gf_log (this->name, GF_LOG_INFO, "Parent dir missing for %s,"
                        " in missing entry self-heal, aborting missing-entry "
                        "self-heal",
                        local->loc.path);
                afr_sh_missing_entries_finish (frame, this);
                return;
        }

        nsources = afr_build_sources (this, sh->xattr, sh->buf,
                                      sh->pending_matrix, sh->sources,
                                      sh->success_children,
                                      AFR_ENTRY_TRANSACTION, &subvol_status,
                                      _gf_true);
        if ((subvol_status & ALL_FOOLS) ||
            (subvol_status & SPLIT_BRAIN)) {
                gf_log (this->name, GF_LOG_INFO, "%s: Performing conservative "
                        "merge", sh->parent_loc.path);
                afr_mark_success_children_sources (sh->sources,
                                                   sh->success_children,
                                                   priv->child_count);
        } else if (nsources < 0) {
                gf_log (this->name, GF_LOG_ERROR, "No sources for dir "
                        "of %s, in missing entry self-heal, aborting "
                        "self-heal", local->loc.path);
                op_errno = EIO;
                goto out;
        }

        source = afr_sh_select_source (sh->sources, priv->child_count);
        if (source == -1) {
                GF_ASSERT (0);
                gf_log (this->name, GF_LOG_DEBUG, "No active sources found.");
                op_errno = EIO;
                goto out;
        }
        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_parent_dirs, priv->child_count);
        afr_sh_common_lookup (frame, this, &local->loc,
                              afr_sh_children_lookup_done, NULL, 0,
                              NULL);
        return;

out:
        afr_sh_set_error (sh, op_errno);
        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
	afr_sh_missing_entries_finish (frame, this);
        return;
}

void
afr_sh_common_reset (afr_self_heal_t *sh, unsigned int child_count)
{
        int             i = 0;

        for (i = 0; i < child_count; i++) {
                memset (&sh->buf[i], 0, sizeof (sh->buf[i]));
                memset (&sh->parentbufs[i], 0, sizeof (sh->parentbufs[i]));
                sh->child_errno[i] = 0;
        }
        memset (&sh->parentbuf, 0, sizeof (sh->parentbuf));
        sh->success_count = 0;
        afr_reset_children (sh->success_children, child_count);
        afr_reset_children (sh->fresh_children, child_count);
        afr_reset_xattr (sh->xattr, child_count);
        loc_wipe (&sh->lookup_loc);
}

/* afr self-heal state will be lost if this call is made
 * please check the afr_sh_common_reset that is called in this function
 */
int
afr_sh_common_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      afr_lookup_done_cbk_t lookup_done , uuid_t gfid,
                      int32_t flags, dict_t *xdata)
{
        afr_local_t    *local = NULL;
        int             i = 0;
        int             call_count = 0;
        afr_private_t  *priv = NULL;
        dict_t         *xattr_req = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        priv  = this->private;
        sh    = &local->self_heal;

        call_count = afr_up_children_count (local->child_up, priv->child_count);

        local->call_count = call_count;

        xattr_req = dict_new();

        if (xattr_req) {
                afr_xattr_req_prepare (this, xattr_req, loc->path);
                if (gfid) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "looking up %s with gfid: %s",
                                loc->path, uuid_utoa (gfid));
                        GF_ASSERT (!uuid_is_null (gfid));
                        afr_set_dict_gfid (xattr_req, gfid);
                }
        }

        afr_sh_common_reset (sh, priv->child_count);
        sh->lookup_done = lookup_done;
        loc_copy (&sh->lookup_loc, loc);
        sh->lookup_flags = flags;
        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "looking up %s on subvolume %s",
                                loc->path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame,
                                           afr_sh_common_lookup_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->lookup,
                                           loc, xattr_req);

                        if (!--call_count)
                                break;
                }
        }

        if (xattr_req)
                dict_unref (xattr_req);

        return 0;
}



int
afr_sh_post_nb_entrylk_missing_entry_sh_cbk (call_frame_t *frame,
                                             xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Non blocking entrylks failed.");
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                afr_sh_missing_entries_done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking entrylks done. Proceeding to FOP");
                afr_sh_common_lookup (frame, this, &sh->parent_loc,
                                      afr_sh_find_fresh_parents,
                                      NULL, AFR_LOOKUP_FAIL_CONFLICTS,
                                      NULL);
        }

        return 0;
}

int
afr_sh_entrylk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                char *base_name, afr_lock_cbk_t lock_cbk)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->transaction_lk_type = AFR_SELFHEAL_LK;
        int_lock->selfheal_lk_type    = AFR_ENTRY_SELF_HEAL_LK;

        afr_set_lock_number (frame, this);

        int_lock->lk_basename = base_name;
        int_lock->lk_loc      = loc;
        int_lock->lock_cbk    = lock_cbk;
        int_lock->domain      = this->name;

        int_lock->lockee_count = 0;
        afr_init_entry_lockee (&int_lock->lockee[0], local, loc,
                               base_name, priv->child_count);
        int_lock->lockee_count++;
        afr_nonblocking_entrylk (frame, this);

        return 0;
}

static int
afr_self_heal_parent_entrylk (call_frame_t *frame, xlator_t *this,
                              afr_lock_cbk_t lock_cbk)
{
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;
        afr_internal_lock_t *int_lock = NULL;
        int                 ret       = -1;
        int32_t             op_errno  = 0;

        local    = frame->local;
        sh       = &local->self_heal;

        gf_log (this->name, GF_LOG_TRACE,
                "attempting to recreate missing entries for path=%s",
                local->loc.path);

        ret = afr_build_parent_loc (&sh->parent_loc, &local->loc, &op_errno);
        if (ret)
                goto out;

        afr_sh_entrylk (frame, this, &sh->parent_loc, NULL,
                        lock_cbk);
        return 0;
out:
        int_lock = &local->internal_lock;
        int_lock->lock_op_ret = -1;
        lock_cbk (frame, this);
        return 0;
}

static int
afr_self_heal_missing_entries (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->sh_type_in_action  = AFR_SELF_HEAL_GFID_OR_MISSING_ENTRY;

        afr_set_self_heal_status (sh, AFR_SELF_HEAL_STARTED);

        afr_self_heal_parent_entrylk (frame, this,
                                      afr_sh_post_nb_entrylk_missing_entry_sh_cbk);
        return 0;
}

afr_local_t*
afr_self_heal_local_init (afr_local_t *l, xlator_t *this)
{
        afr_private_t   *priv  = NULL;
        afr_local_t     *lc    = NULL;
        afr_self_heal_t *sh    = NULL;
        afr_self_heal_t *shc   = NULL;
        int             ret    = 0;

        priv = this->private;

        sh = &l->self_heal;

        lc = mem_get0 (this->local_pool);
        if (!lc)
                goto out;

        shc = &lc->self_heal;

        shc->unwind = sh->unwind;
        shc->gfid_sh_success_cbk = sh->gfid_sh_success_cbk;
        shc->dry_run = sh->dry_run;
        shc->do_missing_entry_self_heal = sh->do_missing_entry_self_heal;
        shc->do_gfid_self_heal = sh->do_gfid_self_heal;
        shc->do_data_self_heal = sh->do_data_self_heal;
        shc->do_metadata_self_heal = sh->do_metadata_self_heal;
        shc->do_entry_self_heal = sh->do_entry_self_heal;
        shc->force_confirm_spb = sh->force_confirm_spb;
        shc->forced_merge = sh->forced_merge;
        shc->background = sh->background;
        shc->type = sh->type;
        shc->data_sh_info = "";
        shc->metadata_sh_info =  "";

        uuid_copy (shc->sh_gfid_req, sh->sh_gfid_req);
        if (l->loc.path) {
                ret = loc_copy (&lc->loc, &l->loc);
                if (ret < 0)
                        goto out;
        }

        lc->child_up  = memdup (l->child_up,
                                sizeof (*lc->child_up) * priv->child_count);
        if (!lc->child_up) {
                ret = -1;
                goto out;
        }

        if (l->xattr_req)
                lc->xattr_req = dict_ref (l->xattr_req);

        if (l->cont.lookup.inode)
                lc->cont.lookup.inode = inode_ref (l->cont.lookup.inode);
        if (l->cont.lookup.xattr)
                lc->cont.lookup.xattr = dict_ref (l->cont.lookup.xattr);

        lc->internal_lock.locked_nodes =
                             GF_CALLOC (sizeof (*l->internal_lock.locked_nodes),
                                        priv->child_count, gf_afr_mt_char);
        if (!lc->internal_lock.locked_nodes) {
                ret = -1;
                goto out;
        }

        ret = afr_inodelk_init (&lc->internal_lock.inodelk[0],
                                this->name, priv->child_count);
        if (ret)
                goto out;
        lc->attempt_self_heal = l->attempt_self_heal;

out:
        if (ret) {
                afr_local_cleanup (lc, this);
                lc = NULL;
        }
        return lc;
}

int
afr_self_heal_completion_cbk (call_frame_t *bgsh_frame, xlator_t *this)
{
        afr_private_t *   priv  = NULL;
        afr_local_t *     local = NULL;
        afr_self_heal_t * sh    = NULL;
        afr_local_t *     orig_frame_local = NULL;
        afr_self_heal_t * orig_frame_sh = NULL;
        char              sh_type_str[256] = {0,};
        gf_loglevel_t     loglevel = 0;

        priv  = this->private;
        local = bgsh_frame->local;
        sh    = &local->self_heal;

        if (local->unhealable) {
                afr_set_split_brain (this, sh->inode, SPB, SPB);
        }

        afr_self_heal_type_str_get (sh, sh_type_str,
                                    sizeof(sh_type_str));
        if (is_self_heal_failed (sh, AFR_CHECK_ALL) && !priv->shd.iamshd) {
                loglevel = GF_LOG_ERROR;
        } else if (!is_self_heal_failed (sh, AFR_CHECK_ALL)) {
                loglevel = GF_LOG_INFO;
        } else {
                loglevel = GF_LOG_DEBUG;
        }

        if (sh->dry_run)
                loglevel = GF_LOG_DEBUG;

        afr_log_self_heal_completion_status (local, loglevel);

        FRAME_SU_UNDO (bgsh_frame, afr_local_t);

        if (!sh->unwound && sh->unwind) {
                orig_frame_local = sh->orig_frame->local;
                orig_frame_sh = &orig_frame_local->self_heal;
                orig_frame_sh->actual_sh_started = _gf_true;
                orig_frame_sh->entry_sh_pending = sh->entry_sh_pending;
                orig_frame_sh->data_sh_pending = sh->data_sh_pending;
                orig_frame_sh->metadata_sh_pending = sh->metadata_sh_pending;
                orig_frame_sh->possibly_healing = sh->possibly_healing;
                sh->unwind (sh->orig_frame, this, sh->op_ret, sh->op_errno,
                            is_self_heal_failed (sh, AFR_CHECK_ALL));
        }

        if (sh->background) {
                LOCK (&priv->lock);
                {
                        priv->background_self_heals_started--;
                }
                UNLOCK (&priv->lock);
        }

        AFR_STACK_DESTROY (bgsh_frame);

        return 0;
}

int
afr_self_heal (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int32_t          op_errno = 0;
        int              ret = 0;
        afr_self_heal_t *orig_sh = NULL;
        call_frame_t    *sh_frame = NULL;
        afr_local_t     *sh_local = NULL;
        loc_t           *loc   = NULL;

        local = frame->local;
        orig_sh = &local->self_heal;
        priv  = this->private;

        GF_ASSERT (local->loc.path);

        gf_log (this->name, GF_LOG_TRACE,
                "performing self heal on %s (metadata=%d data=%d entry=%d)",
                local->loc.path,
                local->self_heal.do_metadata_self_heal,
                local->self_heal.do_data_self_heal,
                local->self_heal.do_entry_self_heal);

        op_errno        = ENOMEM;
        sh_frame        = copy_frame (frame);
        if (!sh_frame)
                goto out;
        afr_set_lk_owner (sh_frame, this, sh_frame->root);
        afr_set_low_priority (sh_frame);

        sh_local        = afr_self_heal_local_init (local, this);
        if (!sh_local)
                goto out;
        sh_frame->local = sh_local;
        sh              = &sh_local->self_heal;

        sh->inode       = inode_ref (inode);
        sh->orig_frame  = frame;

        sh->completion_cbk = afr_self_heal_completion_cbk;

        sh->success = GF_CALLOC (priv->child_count, sizeof (*sh->success),
                                 gf_afr_mt_char);
        if (!sh->success)
                goto out;
        sh->sources = GF_CALLOC (sizeof (*sh->sources), priv->child_count,
                                 gf_afr_mt_int);
        if (!sh->sources)
                goto out;
        sh->locked_nodes = GF_CALLOC (sizeof (*sh->locked_nodes),
                                      priv->child_count,
                                      gf_afr_mt_int);
        if (!sh->locked_nodes)
                goto out;

        sh->pending_matrix = afr_matrix_create (priv->child_count,
                                                priv->child_count);
        if (!sh->pending_matrix)
                goto out;

        sh->delta_matrix = afr_matrix_create (priv->child_count,
                                              priv->child_count);
        if (!sh->delta_matrix)
                goto out;

        sh->fresh_parent_dirs = afr_children_create (priv->child_count);
        if (!sh->fresh_parent_dirs)
                goto out;
        ret = afr_sh_common_create (sh, priv->child_count);
        if (ret) {
                op_errno = -ret;
                goto out;
        }

        if (local->self_heal.background) {
                LOCK (&priv->lock);
                {
                        if (priv->background_self_heals_started
                            < priv->background_self_heal_count) {
                                priv->background_self_heals_started++;


                        } else {
                                local->self_heal.background = _gf_false;
                                sh->background = _gf_false;
                        }
                }
                UNLOCK (&priv->lock);
        }

        if (!local->loc.parent) {
                sh->do_missing_entry_self_heal = _gf_false;
                sh->do_gfid_self_heal = _gf_false;
        }

        sh->sh_type_in_action = AFR_SELF_HEAL_INVALID;

        FRAME_SU_DO (sh_frame, afr_local_t);
        if (afr_can_start_missing_entry_gfid_self_heal (local, priv)) {
                afr_self_heal_missing_entries (sh_frame, this);
        } else {
                loc = &sh_local->loc;
                if (uuid_is_null (loc->inode->gfid) && uuid_is_null (loc->gfid)) {
                        if (!uuid_is_null (inode->gfid))
                                GF_ASSERT (!uuid_compare (inode->gfid,
                                           sh->sh_gfid_req));
                        uuid_copy (loc->gfid, sh->sh_gfid_req);
                }
                gf_log (this->name, GF_LOG_TRACE,
                        "proceeding to metadata check on %s",
                        local->loc.path);

                afr_sh_missing_entries_done (sh_frame, this);
        }
        op_errno = 0;

out:
        if (op_errno) {
                orig_sh->unwind (frame, this, -1, op_errno, 1);
                if (sh_frame)
                        AFR_STACK_DESTROY (sh_frame);
        }
        return 0;
}

void
afr_self_heal_type_str_get (afr_self_heal_t *self_heal_p, char *str,
                            size_t size)
{
        GF_ASSERT (str && (size > strlen (" missing-entry gfid "
                                          "meta-data data entry")));

        if (self_heal_p->do_metadata_self_heal) {
                snprintf (str, size, " meta-data");
        }

        if (self_heal_p->do_data_self_heal) {
                snprintf (str + strlen(str), size - strlen(str), " data");
        }

        if (self_heal_p->do_entry_self_heal) {
                snprintf (str + strlen(str), size - strlen(str), " entry");
        }

        if (self_heal_p->do_missing_entry_self_heal) {
                snprintf (str + strlen(str), size - strlen(str),
                         " missing-entry");
        }

        if (self_heal_p->do_gfid_self_heal) {
                snprintf (str + strlen(str), size - strlen(str), " gfid");
        }
}

afr_self_heal_type
afr_self_heal_type_for_transaction (afr_transaction_type type)
{
        afr_self_heal_type sh_type = AFR_SELF_HEAL_INVALID;

        switch (type) {
        case AFR_DATA_TRANSACTION:
                sh_type = AFR_SELF_HEAL_DATA;
                break;
        case AFR_METADATA_TRANSACTION:
                sh_type = AFR_SELF_HEAL_METADATA;
                break;
        case AFR_ENTRY_TRANSACTION:
                sh_type = AFR_SELF_HEAL_ENTRY;
                break;
        case AFR_ENTRY_RENAME_TRANSACTION:
                GF_ASSERT (0);
                break;
        }
        return sh_type;
}

int
afr_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name)
{
        int   ret = -1;
        uuid_t pargfid = {0};

        if (!child)
                goto out;

        if (!uuid_is_null (parent->inode->gfid))
                uuid_copy (pargfid, parent->inode->gfid);
        else if (!uuid_is_null (parent->gfid))
                uuid_copy (pargfid, parent->gfid);

        if (uuid_is_null (pargfid))
                goto out;

        if (strcmp (parent->path, "/") == 0)
                ret = gf_asprintf ((char **)&child->path, "/%s", name);
        else
                ret = gf_asprintf ((char **)&child->path, "%s/%s", parent->path,
                                   name);

        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "asprintf failed while setting child path");
        }

        child->name = strrchr (child->path, '/');
        if (child->name)
                child->name++;

        child->parent = inode_ref (parent->inode);
        child->inode = inode_new (parent->inode->table);
        uuid_copy (child->pargfid, pargfid);

        if (!child->inode) {
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        if ((ret == -1) && child)
                loc_wipe (child);

        return ret;
}

int
afr_sh_erase_pending (call_frame_t *frame, xlator_t *this,
                      afr_transaction_type type, afr_fxattrop_cbk_t cbk,
                      int (*finish)(call_frame_t *frame, xlator_t *this))
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              call_count = 0;
        int              i = 0;
        dict_t          **erase_xattr = NULL;
        int             ret = -1;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        afr_sh_pending_to_delta (priv, sh->xattr, sh->delta_matrix,
                                 sh->success, priv->child_count, type);

        erase_xattr = GF_CALLOC (sizeof (*erase_xattr), priv->child_count,
                                 gf_afr_mt_dict_t);
        if (!erase_xattr)
                goto out;

        for (i = 0; i < priv->child_count; i++) {
                if (sh->xattr[i]) {
                        call_count++;
                        erase_xattr[i] = dict_new ();
                        if (!erase_xattr[i])
                                goto out;
                }
        }

        afr_sh_delta_to_xattr (this, sh->delta_matrix, erase_xattr,
                               priv->child_count, type);

        gf_log (this->name, GF_LOG_DEBUG, "Delta matrix for: %s",
                lkowner_utoa (&frame->root->lk_owner));
        afr_sh_print_pending_matrix (sh->delta_matrix, this);
        local->call_count = call_count;
        if (call_count == 0) {
                ret = 0;
                finish (frame, this);
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!erase_xattr[i])
                        continue;

                if (sh->healing_fd) {//true for ENTRY, reg file DATA transaction
                        STACK_WIND_COOKIE (frame, cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fxattrop,
                                           sh->healing_fd,
                                           GF_XATTROP_ADD_ARRAY, erase_xattr[i],
                                           NULL);
                } else {
                        STACK_WIND_COOKIE (frame, cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->xattrop,
                                           &local->loc,
                                           GF_XATTROP_ADD_ARRAY, erase_xattr[i],
                                           NULL);
                }
        }

        ret = 0;
out:
        if (erase_xattr) {
                for (i = 0; i < priv->child_count; i++) {
                        if (erase_xattr[i]) {
                                dict_unref (erase_xattr[i]);
                        }
                }
        }

        GF_FREE (erase_xattr);

        if (ret < 0) {
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                finish (frame, this);
        }

        return 0;
}

void
afr_set_self_heal_status(afr_self_heal_t *sh, afr_self_heal_status status)
{
        xlator_t                *this = NULL;
        afr_sh_status_for_all_type *sh_status = &(sh->afr_all_sh_status);
        afr_self_heal_type  sh_type_in_action = sh->sh_type_in_action;
        this = THIS;

        if (!sh) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Null self heal"
                                  "Structure");
                goto out;
        }

        switch (sh_type_in_action) {
                case AFR_SELF_HEAL_GFID_OR_MISSING_ENTRY:
                       sh_status->gfid_or_missing_entry_self_heal = status;
                        break;
                case AFR_SELF_HEAL_METADATA:
                        sh_status->metadata_self_heal = status;
                        break;
                case AFR_SELF_HEAL_DATA:
                        sh_status->data_self_heal = status;
                        break;
                case AFR_SELF_HEAL_ENTRY:
                        sh_status->entry_self_heal = status;
                        break;
                case AFR_SELF_HEAL_INVALID:
                        gf_log_callingfn (this->name, GF_LOG_ERROR, "Invalid"
                                          "self heal type in action");
                        break;
        }
out:
        return;
}

void
afr_set_local_for_unhealable (afr_local_t *local)
{
        afr_self_heal_t  *sh = NULL;

        sh = &local->self_heal;

        local->unhealable = 1;
        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
}

int
is_self_heal_failed (afr_self_heal_t *sh, afr_sh_fail_check_type type)
{
        afr_sh_status_for_all_type      sh_status = sh->afr_all_sh_status;
        afr_self_heal_type   sh_type_in_action =  AFR_SELF_HEAL_INVALID;
        afr_self_heal_status    status = AFR_SELF_HEAL_FAILED;
        xlator_t                *this = NULL;
        int                     sh_failed = 0;

        this = THIS;

        if (!sh) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Null self heal "
                                  "structure");
                sh_failed = 1;
                goto out;
        }

        if (type == AFR_CHECK_ALL) {
                if ((sh_status.gfid_or_missing_entry_self_heal == AFR_SELF_HEAL_FAILED)
                    || (sh_status.metadata_self_heal == AFR_SELF_HEAL_FAILED)
                    || (sh_status.data_self_heal == AFR_SELF_HEAL_FAILED)
                    || (sh_status.entry_self_heal == AFR_SELF_HEAL_FAILED))
                sh_failed = 1;
        } else if (type == AFR_CHECK_SPECIFIC) {
                sh_type_in_action = sh->sh_type_in_action;
                switch (sh_type_in_action) {
                        case AFR_SELF_HEAL_GFID_OR_MISSING_ENTRY:
                             status = sh_status.gfid_or_missing_entry_self_heal;
                                break;
                        case AFR_SELF_HEAL_METADATA:
                                status = sh_status.metadata_self_heal;
                                break;
                        case AFR_SELF_HEAL_ENTRY:
                                status = sh_status.entry_self_heal;
                                break;
                        case AFR_SELF_HEAL_DATA:
                                status = sh_status.data_self_heal;
                                break;
                        case AFR_SELF_HEAL_INVALID:
                                status = AFR_SELF_HEAL_NOT_ATTEMPTED;
                                break;
                }
                if (status == AFR_SELF_HEAL_FAILED)
                        sh_failed = 1;

        }

out:
        return sh_failed;
}

char *
get_sh_completion_status (afr_self_heal_status status)
{

        char *not_attempted       = " is not attempted";
        char *failed              = " failed";
        char *started             = " is started";
        char *sync_begin          = " is successfully completed";
        char *result              = " has unknown status";

        switch (status)
        {
                case AFR_SELF_HEAL_NOT_ATTEMPTED:
                        result = not_attempted;
                        break;
                case AFR_SELF_HEAL_FAILED:
                        result = failed;
                        break;
                case AFR_SELF_HEAL_STARTED:
                        result = started;
                        break;
                case AFR_SELF_HEAL_SYNC_BEGIN:
                        result = sync_begin;
                        break;
        }

        return result;

}

void
afr_log_self_heal_completion_status (afr_local_t *local, gf_loglevel_t loglvl)
{

        char sh_log[4096]              = {0};
        afr_self_heal_t *sh            = &local->self_heal;
        afr_sh_status_for_all_type   all_status = sh->afr_all_sh_status;
        xlator_t      *this            = NULL;
        size_t        off              = 0;
        int           data_sh          = 0;
        int           metadata_sh      = 0;
        int           print_log        = 0;

        this = THIS;

        ADD_FMT_STRING (sh_log, off, "gfid or missing entry",
                        all_status.gfid_or_missing_entry_self_heal, print_log);
        ADD_FMT_STRING_SYNC (sh_log, off, "metadata",
                             all_status.metadata_self_heal, print_log);
        if (sh->background) {
                ADD_FMT_STRING_SYNC (sh_log, off, "backgroung data",
                                all_status.data_self_heal, print_log);
        } else {
                ADD_FMT_STRING_SYNC (sh_log, off, "foreground data",
                                all_status.data_self_heal, print_log);
        }
        ADD_FMT_STRING_SYNC (sh_log, off, "entry", all_status.entry_self_heal,
                             print_log);

        if (AFR_SELF_HEAL_SYNC_BEGIN == all_status.data_self_heal &&
	    strcmp (sh->data_sh_info, "") && sh->data_sh_info )
                data_sh = 1;
        if (AFR_SELF_HEAL_SYNC_BEGIN == all_status.metadata_self_heal &&
	    strcmp (sh->metadata_sh_info, "") && sh->metadata_sh_info)
                metadata_sh = 1;

        if (!print_log)
                return;

        gf_log (this->name, loglvl, "%s %s %s on %s", sh_log,
                ((data_sh == 1) ? sh->data_sh_info : ""),
                ((metadata_sh == 1) ? sh->metadata_sh_info : ""),
                local->loc.path);
}
