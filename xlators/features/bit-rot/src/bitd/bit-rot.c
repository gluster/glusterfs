/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <ctype.h>
#include <sys/uio.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "compat-errno.h"

#include "bit-rot.h"
#include "bit-rot-scrub.h"
#include <pthread.h>
#include "bit-rot-bitd-messages.h"

#include "tw.h"

#define BR_HASH_CALC_READ_SIZE  (128 * 1024)

typedef int32_t (br_child_handler)(xlator_t *, br_child_t *);

struct br_child_event {
        xlator_t *this;

        br_child_t *child;

        br_child_handler *call;

        struct list_head list;
};

static int
br_find_child_index (xlator_t *this, xlator_t *child)
{
        br_private_t *priv   = NULL;
        int           i      = -1;
        int           index  = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (child == priv->children[i].xl) {
                        index = i;
                        break;
                }
        }

out:
        return index;
}

br_child_t *
br_get_child_from_brick_path (xlator_t *this, char *brick_path)
{
        br_private_t *priv  = NULL;
        br_child_t   *child = NULL;
        br_child_t   *tmp   = NULL;
        int           i     = 0;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, brick_path, out);

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                for (i = 0; i < priv->child_count; i++) {
                        tmp = &priv->children[i];
                        if (!strcmp (tmp->brick_path, brick_path)) {
                                child = tmp;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&priv->lock);

out:
        return child;
}

/**
 * probably we'll encapsulate brick inside our own structure when
 * needed -- later.
 */
void *
br_brick_init (void *xl, struct gf_brick_spec *brick)
{
        return brick;
}

/**
 * and cleanup things here when allocated br_brick_init().
 */
void
br_brick_fini (void *xl, char *brick, void *data)
{
        return;
}

/**
 * TODO: Signature can contain null terminators which causes bitrot
 * stub to store truncated hash as it depends on string length of
 * the hash.
 *
 * FIX: Send the string length as part of the signature struct and
 *      change stub to handle this change.
 */
static br_isignature_t *
br_prepare_signature (const unsigned char *sign,
                      unsigned long hashlen,
                      int8_t hashtype, br_object_t *object)
{
        br_isignature_t *signature = NULL;

        /* TODO: use mem-pool */
        signature = GF_CALLOC (1, signature_size (hashlen + 1),
                               gf_br_stub_mt_signature_t);
        if (!signature)
                return NULL;

        /* object version */
        signature->signedversion = object->signedversion;

        /* signature length & type */
        signature->signaturelen = hashlen;
        signature->signaturetype = hashtype;

        /* signature itself */
        memcpy (signature->signature, (char *)sign, hashlen);
        signature->signature[hashlen+1] = '\0';

        return signature;
}

gf_boolean_t
bitd_is_bad_file (xlator_t *this, br_child_t *child, loc_t *loc, fd_t *fd)
{
        int32_t       ret      = -1;
        dict_t       *xattr    = NULL;
        inode_t      *inode    = NULL;
        gf_boolean_t  bad_file = _gf_false;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);

        inode = (loc) ? loc->inode : fd->inode;

        if (fd)
                ret = syncop_fgetxattr (child->xl, fd, &xattr,
                                        BITROT_OBJECT_BAD_KEY, NULL, NULL);
        else if (loc)
                ret = syncop_getxattr (child->xl, loc,
                                       &xattr, BITROT_OBJECT_BAD_KEY, NULL,
                                       NULL);

        if (!ret) {
                gf_msg_debug (this->name, 0, "[GFID: %s] is marked corrupted",
                              uuid_utoa (inode->gfid));
                bad_file = _gf_true;
        }

        if (xattr)
                dict_unref (xattr);

out:
        return bad_file;
}

/**
 * Do a lookup on the gfid present within the object.
 */
static int32_t
br_object_lookup (xlator_t *this, br_object_t *object,
                  struct iatt *iatt, inode_t **linked_inode)
{
	int      ret          = -EINVAL;
	loc_t    loc          = {0, };
	inode_t *inode        = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, object, out);

	inode = inode_find (object->child->table, object->gfid);

        if (inode)
                loc.inode = inode;
        else
                loc.inode = inode_new (object->child->table);

	if (!loc.inode) {
                ret = -ENOMEM;
		goto out;
        }

	gf_uuid_copy (loc.gfid, object->gfid);

	ret = syncop_lookup (object->child->xl, &loc, iatt, NULL, NULL, NULL);
	if (ret < 0)
		goto out;

        /*
         * The file might have been deleted by the application
         * after getting the event, but before doing a lookup.
         * So use linked_inode after inode_link is done.
         */
	*linked_inode = inode_link (loc.inode, NULL, NULL, iatt);
	if (*linked_inode)
		inode_lookup (*linked_inode);

out:
	loc_wipe (&loc);
	return ret;
}

/**
 * open the object with O_RDONLY flags and return the fd. How to let brick
 * know that open is being done by bitd because syncop framework does not allow
 * passing xdata -- may be use frame->root->pid itself.
 */
static int32_t
br_object_open (xlator_t *this,
                br_object_t *object, inode_t *inode, fd_t **openfd)
{
        int32_t      ret   = -1;
        fd_t        *fd   = NULL;
        loc_t        loc   = {0, };

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, object, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = -EINVAL;
        fd = fd_create (inode, 0);
        if (!fd) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_FD_CREATE_FAILED,
                        "failed to create fd for the inode %s",
                        uuid_utoa (inode->gfid));
                goto out;
        }

        loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

        ret = syncop_open (object->child->xl, &loc, O_RDONLY, fd, NULL, NULL);
	if (ret) {
                br_log_object (this, "open", inode->gfid, -ret);
		fd_unref (fd);
		fd = NULL;
	} else {
		fd_bind (fd);
                *openfd = fd;
	}

        loc_wipe (&loc);

out:
        return ret;
}

/**
 * read 128k block from the object @object from the offset @offset
 * and return the buffer.
 */
static int32_t
br_object_read_block_and_sign (xlator_t *this, fd_t *fd, br_child_t *child,
                               off_t offset, size_t size, SHA256_CTX *sha256)
{
        int32_t        ret    = -1;
        tbf_t      *tbf    = NULL;
        struct iovec  *iovec  = NULL;
        struct iobref *iobref = NULL;
        br_private_t  *priv   = NULL;
        int            count  = 0;
        int            i      = 0;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        priv = this->private;

        GF_VALIDATE_OR_GOTO (this->name, priv->tbf, out);
        tbf = priv->tbf;

        ret = syncop_readv (child->xl, fd,
                            size, offset, 0, &iovec, &count, &iobref, NULL,
                            NULL);

        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno, BRB_MSG_READV_FAILED,
                        "readv on %s failed", uuid_utoa (fd->inode->gfid));
                ret = -1;
                goto out;
        }

        if (ret == 0)
                goto out;

        for (i = 0; i < count; i++) {
                TBF_THROTTLE_BEGIN (tbf, TBF_OP_HASH, iovec[i].iov_len);
                {
                        SHA256_Update (sha256, (const unsigned char *)
                                       (iovec[i].iov_base), iovec[i].iov_len);
                }
                TBF_THROTTLE_BEGIN (tbf, TBF_OP_HASH, iovec[i].iov_len);
        }

 out:
        if (iovec)
                GF_FREE (iovec);

        if (iobref)
                iobref_unref (iobref);

        return ret;
}

int32_t
br_calculate_obj_checksum (unsigned char *md,
                           br_child_t *child, fd_t *fd, struct iatt *iatt)
{
        int32_t   ret    = -1;
        off_t     offset = 0;
        size_t    block  = BR_HASH_CALC_READ_SIZE;
        xlator_t *this   = NULL;

        SHA256_CTX       sha256;

        GF_VALIDATE_OR_GOTO ("bit-rot", child, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", iatt, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", fd, out);

        this = child->this;

        SHA256_Init (&sha256);

        while (1) {
                ret = br_object_read_block_and_sign (this, fd, child,
                                                     offset, block, &sha256);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRB_MSG_BLOCK_READ_FAILED, "reading block with "
                                "offset %lu of object %s failed", offset,
                                uuid_utoa (fd->inode->gfid));
                        break;
                }

                if (ret == 0)
                        break;

                offset += ret;
        }

        if (ret == 0)
                SHA256_Final (md, &sha256);

 out:
        return ret;
}

static int32_t
br_object_checksum (unsigned char *md,
                    br_object_t *object, fd_t *fd, struct iatt *iatt)
{
        return br_calculate_obj_checksum (md, object->child, fd,  iatt);
}

static int32_t
br_object_read_sign (inode_t *linked_inode, fd_t *fd, br_object_t *object,
                     struct iatt *iatt)
{
        int32_t          ret           = -1;
        xlator_t        *this          = NULL;
        dict_t          *xattr         = NULL;
        unsigned char   *md            = NULL;
        br_isignature_t *sign          = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", object, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", linked_inode, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", fd, out);

        this = object->this;

        md = GF_CALLOC (SHA256_DIGEST_LENGTH, sizeof (*md), gf_common_mt_char);
        if (!md) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, BRB_MSG_NO_MEMORY,
                        "failed to allocate memory for saving hash of the "
                        "object %s", uuid_utoa (fd->inode->gfid));
                goto out;
        }

        ret = br_object_checksum (md, object, fd, iatt);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRB_MSG_CALC_CHECKSUM_FAILED, "calculating checksum "
                        "for the object %s failed",
                        uuid_utoa (linked_inode->gfid));
                goto free_signature;
        }

        sign = br_prepare_signature (md, SHA256_DIGEST_LENGTH,
                                     BR_SIGNATURE_TYPE_SHA256, object);
        if (!sign) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_GET_SIGN_FAILED,
                        "failed to get the signature for the object %s",
                        uuid_utoa (fd->inode->gfid));
                goto free_signature;
        }

        xattr = dict_for_key_value
                (GLUSTERFS_SET_OBJECT_SIGNATURE,
                 (void *)sign, signature_size (SHA256_DIGEST_LENGTH));

        if (!xattr) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_SET_SIGN_FAILED,
                        "dict allocation for signing failed for the object %s",
                        uuid_utoa (fd->inode->gfid));
                goto free_isign;
        }

        ret = syncop_fsetxattr (object->child->xl, fd, xattr, 0, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_SET_SIGN_FAILED,
                        "fsetxattr of signature to the object %s failed",
                        uuid_utoa (fd->inode->gfid));
                goto unref_dict;
        }

        ret = 0;

 unref_dict:
        dict_unref (xattr);
 free_isign:
        GF_FREE (sign);
 free_signature:
        GF_FREE (md);
 out:
        return ret;
}

static int br_object_sign_softerror (int32_t op_errno)
{
        return ((op_errno == ENOENT) || (op_errno == ESTALE)
                || (op_errno == ENODATA));
}

void
br_log_object (xlator_t *this, char *op, uuid_t gfid, int32_t op_errno)
{
        int softerror = br_object_sign_softerror (op_errno);
        if (softerror) {
                gf_msg_debug (this->name, 0, "%s() failed on object %s "
                              "[reason: %s]", op, uuid_utoa (gfid),
                              strerror (op_errno));
        } else {
                gf_msg (this->name, GF_LOG_ERROR, op_errno, BRB_MSG_OP_FAILED,
                        "%s() failed on object %s", op, uuid_utoa (gfid));
        }
}

void
br_log_object_path (xlator_t *this, char *op,
                    const char *path, int32_t op_errno)
{
        int softerror = br_object_sign_softerror (op_errno);
        if (softerror) {
                gf_msg_debug (this->name, 0, "%s() failed on object %s "
                              "[reason: %s]", op, path, strerror (op_errno));
        } else {
                gf_msg (this->name, GF_LOG_ERROR, op_errno, BRB_MSG_OP_FAILED,
                        "%s() failed on object %s", op, path);
        }
}

static void
br_trigger_sign (xlator_t *this, br_child_t *child,
                 inode_t *linked_inode, loc_t *loc, gf_boolean_t need_reopen)
{
        fd_t     *fd   = NULL;
        int32_t   ret  = -1;
        uint32_t  val  = 0;
        dict_t   *dict = NULL;
        pid_t     pid  = GF_CLIENT_PID_BITD;

        syncopctx_setfspid (&pid);

        val = (need_reopen == _gf_true) ? BR_OBJECT_REOPEN : BR_OBJECT_RESIGN;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_uint32 (dict, BR_REOPEN_SIGN_HINT_KEY, val);
        if (ret)
                goto cleanup_dict;

        ret = -1;
        fd = fd_create (linked_inode, 0);
        if (!fd) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_FD_CREATE_FAILED,
                        "Failed to create fd [GFID %s]",
                        uuid_utoa (linked_inode->gfid));
                goto cleanup_dict;
        }

        ret = syncop_open (child->xl, loc, O_RDWR, fd, NULL, NULL);
	if (ret) {
                br_log_object (this, "open", linked_inode->gfid, -ret);
                goto unref_fd;
	}

        fd_bind (fd);

        ret = syncop_fsetxattr (child->xl, fd, dict, 0, NULL, NULL);
        if (ret)
                br_log_object (this, "fsetxattr", linked_inode->gfid, -ret);

        /* passthough: fd_unref() */

 unref_fd:
        fd_unref (fd);
 cleanup_dict:
        dict_unref (dict);
 out:
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, BRB_MSG_TRIGGER_SIGN,
                        "Could not trigger signingd for %s (reopen hint: %d)",
                        uuid_utoa (linked_inode->gfid), val);
        }
}

static void
br_object_resign (xlator_t *this,
                  br_object_t *object, inode_t *linked_inode)
{
        loc_t loc = {0, };

        loc.inode = inode_ref (linked_inode);
        gf_uuid_copy (loc.gfid, linked_inode->gfid);

        br_trigger_sign (this, object->child, linked_inode, &loc, _gf_false);

        loc_wipe (&loc);
}

/**
 * Sign a given object. This routine runs full throttle. There needs to be
 * some form of priority scheduling and/or read burstness to avoid starving
 * (or kicking) client I/O's.
 */
static int32_t br_sign_object (br_object_t *object)
{
        int32_t         ret           = -1;
        inode_t        *linked_inode  = NULL;
        xlator_t       *this          = NULL;
        fd_t           *fd            = NULL;
        struct iatt     iatt          = {0, };
        pid_t           pid           = GF_CLIENT_PID_BITD;
        br_sign_state_t sign_info     = BR_SIGN_NORMAL;

        GF_VALIDATE_OR_GOTO ("bit-rot", object, out);

        this = object->this;

        /**
         * FIXME: This is required as signing an object is restricted to
         * clients with special frame->root->pid. Change the way client
         * pid is set.
         */
        syncopctx_setfspid (&pid);

        ret = br_object_lookup (this, object, &iatt, &linked_inode);
        if (ret) {
                br_log_object (this, "lookup", object->gfid, -ret);
                goto out;
        }

        /**
         * For fd's that have notified for reopening, we send an explicit
         * open() followed by a dummy write() call. This triggers the
         * actual signing of the object.
         */
        sign_info = ntohl (object->sign_info);
        if (sign_info == BR_SIGN_REOPEN_WAIT) {
                br_object_resign (this, object, linked_inode);
                goto unref_inode;
        }

        ret = br_object_open (this, object, linked_inode, &fd);
        if (!fd) {
                br_log_object (this, "open", object->gfid, -ret);
                goto unref_inode;
        }

        /**
         * we have an open file descriptor on the object. from here on,
         * do not be generous to file operation errors.
         */
        gf_msg_debug (this->name, 0, "Signing object [%s]",
                      uuid_utoa (linked_inode->gfid));

        ret = br_object_read_sign (linked_inode, fd, object, &iatt);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRB_MSG_READ_AND_SIGN_FAILED, "reading and signing of "
                        "the object %s failed", uuid_utoa (linked_inode->gfid));
                goto unref_fd;
        }

        ret = 0;

 unref_fd:
        fd_unref (fd);
 unref_inode:
        inode_unref (linked_inode);
 out:
        return ret;
}

static br_object_t *__br_pick_object (br_private_t *priv)
{
        br_object_t *object = NULL;

        while (list_empty (&priv->obj_queue->objects)) {
                pthread_cond_wait (&priv->object_cond, &priv->lock);
        }

        object = list_first_entry
                (&priv->obj_queue->objects, br_object_t, list);
        list_del_init (&object->list);

        return object;
}

/**
 * This is the place where the signing of the objects is triggered.
 */
void *
br_process_object (void *arg)
{
        xlator_t     *this   = NULL;
        br_object_t  *object = NULL;
        br_private_t *priv   = NULL;
        int32_t       ret    = -1;

        this = arg;
        priv = this->private;

        THIS = this;

        for (;;) {
                pthread_mutex_lock (&priv->lock);
                {
                        object = __br_pick_object (priv);
                }
                pthread_mutex_unlock (&priv->lock);

                ret = br_sign_object (object);
                if (ret && !br_object_sign_softerror (-ret))
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRB_MSG_SIGN_FAILED, "SIGNING FAILURE [%s]",
                                uuid_utoa (object->gfid));
                GF_FREE (object);
        }

        return NULL;
}

/**
 * This function gets kicked in once the object is expired from the
 * timer wheel. This actually adds the object received via notification
 * from the changelog to the queue from where the objects gets picked
 * up for signing.
 *
 * This routine can be made lightweight by introducing an alternate
 * timer-wheel API that dispatches _all_ expired objects in one-shot
 * rather than an object at-a-time. This routine can then just simply
 * be a call to list_splice_tail().
 *
 * NOTE: use call_time to instrument signing time in br_sign_object().
 */
void
br_add_object_to_queue (struct gf_tw_timer_list *timer,
                        void *data, unsigned long call_time)
{
        br_object_t   *object = NULL;
        xlator_t      *this   = NULL;
        br_private_t  *priv   = NULL;

        object = data;
        this   = object->this;
        priv   = this->private;

        THIS = this;

        pthread_mutex_lock (&priv->lock);
        {
                list_add_tail (&object->list, &priv->obj_queue->objects);
                pthread_cond_broadcast (&priv->object_cond);
        }
        pthread_mutex_unlock (&priv->lock);

        if (timer)
                mem_put (timer);
        return;
}

static br_object_t *
br_initialize_object (xlator_t *this, br_child_t *child, changelog_event_t *ev)
{
        br_object_t *object = NULL;

        object = GF_CALLOC (1, sizeof (*object), gf_br_mt_br_object_t);
        if (!object)
                goto out;
        INIT_LIST_HEAD (&object->list);

        object->this  = this;
        object->child = child;
        gf_uuid_copy (object->gfid, ev->u.releasebr.gfid);

        /* NOTE: it's BE, but no worry */
        object->signedversion = ev->u.releasebr.version;
        object->sign_info = ev->u.releasebr.sign_info;

out:
        return object;
}

static struct gf_tw_timer_list *
br_initialize_timer (xlator_t *this, br_object_t *object, br_child_t *child,
                     changelog_event_t *ev)
{
        br_private_t  *priv = NULL;
        struct gf_tw_timer_list *timer = NULL;

        priv = this->private;

        timer = mem_get0 (child->timer_pool);
        if (!timer)
                goto out;
        INIT_LIST_HEAD (&timer->entry);

        timer->expires = priv->expiry_time;
        if (!timer->expires)
                timer->expires = 1;

        timer->data     = object;
        timer->function = br_add_object_to_queue;
        gf_tw_add_timer (priv->timer_wheel, timer);

out:
        return timer;
}

static int32_t
br_schedule_object_reopen (xlator_t *this, br_object_t *object,
                           br_child_t *child, changelog_event_t *ev)
{
        struct gf_tw_timer_list *timer = NULL;

        timer = br_initialize_timer (this, object, child, ev);
        if (!timer)
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_SET_TIMER_FAILED,
                        "Failed to allocate object expiry timer [GFID: %s]",
                        uuid_utoa (object->gfid));
        return timer ? 0 : -1;
}

static int32_t
br_object_quicksign (xlator_t *this, br_object_t *object)
{
        br_add_object_to_queue (NULL, object, 0ULL);
        return 0;
}

/**
 * This callback function registered with the changelog is executed
 * whenever a notification from the changelog is received. This should
 * add the object (or the gfid) on which the notification has come to
 * the timer-wheel with some expiry time.
 *
 * TODO: use mem-pool for allocations and maybe allocate timer and
 * object as a single alloc and bifurcate their respective pointers.
 */
void
br_brick_callback (void *xl, char *brick,
                   void *data, changelog_event_t *ev)
{
        int32_t          ret       = 0;
        uuid_t           gfid      = {0,};
        xlator_t        *this      = NULL;
        br_object_t     *object    = NULL;
        br_child_t      *child     = NULL;
        br_sign_state_t  sign_info = BR_SIGN_INVALID;

        this = xl;

        GF_VALIDATE_OR_GOTO (this->name, ev, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        GF_ASSERT (ev->ev_type == CHANGELOG_OP_TYPE_BR_RELEASE);
        GF_ASSERT (!gf_uuid_is_null (ev->u.releasebr.gfid));

        gf_uuid_copy (gfid, ev->u.releasebr.gfid);

        gf_msg_debug (this->name, 0, "RELEASE EVENT [GFID %s]",
                      uuid_utoa (gfid));

        child = br_get_child_from_brick_path (this, brick);
        if (!child) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_GET_SUBVOL_FAILED,
                        "failed to get the subvolume for the brick %s", brick);
                goto out;
        }

        object = br_initialize_object (this, child, ev);
        if (!object) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, BRB_MSG_NO_MEMORY,
                        "failed to allocate object memory [GFID: %s]",
                        uuid_utoa (gfid));
                goto out;
        }

        /* sanity check */
        sign_info = ntohl (object->sign_info);
        GF_ASSERT (sign_info != BR_SIGN_NORMAL);

        if (sign_info == BR_SIGN_REOPEN_WAIT)
                ret = br_schedule_object_reopen (this, object, child, ev);
        else
                ret = br_object_quicksign (this, object);

        if (ret)
                goto free_object;

        gf_msg_debug (this->name, 0, "->callback: brick [%s], type [%d]\n",
                      brick, ev->ev_type);
        return;

 free_object:
        GF_FREE (object);
 out:
        return;
}

void
br_fill_brick_spec (struct gf_brick_spec *brick, char *path)
{
        brick->brick_path = gf_strdup (path);
        brick->filter = CHANGELOG_OP_TYPE_BR_RELEASE;

        brick->init         = br_brick_init;
        brick->fini         = br_brick_fini;
        brick->callback     = br_brick_callback;
        brick->connected    = NULL;
        brick->disconnected = NULL;
}

static gf_boolean_t
br_check_object_need_sign (xlator_t *this, dict_t *xattr, br_child_t *child)
{
        int32_t              ret       = -1;
        gf_boolean_t         need_sign = _gf_false;
        br_isignature_out_t *sign      = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, xattr, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);

        ret = dict_get_ptr (xattr, GLUSTERFS_GET_OBJECT_SIGNATURE,
                            (void **)&sign);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_GET_SIGN_FAILED,
                        "failed to get object signature info");
                goto out;
        }

        /* Object has been opened and hence dirty. Do not sign it */
        if (sign->stale)
                need_sign = _gf_true;

out:
        return need_sign;
}



int32_t
br_prepare_loc (xlator_t *this, br_child_t *child, loc_t *parent,
                gf_dirent_t *entry, loc_t *loc)
{
        int32_t  ret   = -1;
        inode_t *inode = NULL;

        inode = inode_grep (child->table, parent->inode, entry->d_name);
        if (!inode)
                loc->inode = inode_new (child->table);
        else {
                loc->inode = inode;
                if (loc->inode->ia_type != IA_IFREG) {
                        gf_msg_debug (this->name, 0, "%s is not a regular "
                                      "file", entry->d_name);
                        ret = 0;
                        goto out;
                }
        }

        loc->parent = inode_ref (parent->inode);
        gf_uuid_copy (loc->pargfid, parent->inode->gfid);

        ret = inode_path (parent->inode, entry->d_name, (char **)&loc->path);
        if (ret < 0 || !loc->path) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_PATH_FAILED,
                        "inode_path on %s (parent: %s) failed", entry->d_name,
                        uuid_utoa (parent->inode->gfid));
                goto out;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;

        ret = 1;

out:
        return ret;
}

/**
 * Oneshot crawler
 * ---------------
 * This is a catchup mechanism. Objects that remained unsigned from the
 * last run for whatever reason (node crashes, reboots, etc..) become
 * candidates for signing. This allows the signature to "catch up" with
 * the current state of the object. Triggering signing is easy: perform
 * an open() followed by a close() therby resulting in call boomerang.
 * (though not back to itself :))
 */
int
bitd_oneshot_crawl (xlator_t *subvol,
                    gf_dirent_t *entry, loc_t *parent, void *data)
{
        int           op_errno     = 0;
        br_child_t   *child        = NULL;
        xlator_t     *this         = NULL;
        loc_t         loc          = {0, };
        struct iatt   iatt         = {0, };
        struct iatt   parent_buf   = {0, };
        dict_t       *xattr        = NULL;
        int32_t       ret          = -1;
        inode_t      *linked_inode = NULL;
        gf_boolean_t  need_signing = _gf_false;

        GF_VALIDATE_OR_GOTO ("bit-rot", subvol, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", data, out);

        child = data;
        this = child->this;

        ret = br_prepare_loc (this, child, parent, entry, &loc);
        if (!ret)
                goto out;

        ret = syncop_lookup (child->xl, &loc, &iatt, &parent_buf, NULL, NULL);
        if (ret) {
                br_log_object_path (this, "lookup", loc.path, -ret);
                goto out;
        }

        linked_inode = inode_link (loc.inode, parent->inode, loc.name, &iatt);
        if (linked_inode)
                inode_lookup (linked_inode);

        if (iatt.ia_type != IA_IFREG) {
                gf_msg_debug (this->name, 0,  "%s is not a regular file, "
                              "skipping..", entry->d_name);
                ret = 0;
                goto unref_inode;
        }

        /**
         * As of now, 2 cases  are possible and handled.
         * 1) GlusterFS is upgraded from a previous version which does not
         *    have any idea about bit-rot and have data in the filesystem.
         *    In this case syncop_getxattr fails with ENODATA and the object
         *    is signed. (In real, when crawler sends lookup, bit-rot-stub
         *    creates the xattrs before returning lookup reply)
         * 2) Bit-rot was not enabled or BitD was dows for some reasons, during
         *    which some files were created, but since BitD was down, were not
         *    signed.
         * If the file was just created and was being written some data when
         * the down BitD came up, then bit-rot stub should be intelligent to
         * identify this case (by comparing the ongoing version or by checking
         * if there are any fds present for that inode) and handle properly.
         */

        if (bitd_is_bad_file (this, child, &loc, NULL)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, BRB_MSG_SKIP_OBJECT,
                        "Entry [%s] is marked corrupted.. skipping.", loc.path);
                goto unref_inode;
        }

        ret = syncop_getxattr (child->xl, &loc, &xattr,
                               GLUSTERFS_GET_OBJECT_SIGNATURE, NULL, NULL);
        if (ret < 0) {
                op_errno = -ret;
                br_log_object (this, "getxattr", linked_inode->gfid, op_errno);

                /**
                 * No need to sign the zero byte objects as the signing
                 * happens upon first modification of the object.
                 */
                if (op_errno == ENODATA && (iatt.ia_size != 0))
                        need_signing = _gf_true;
                if (op_errno == EINVAL)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                BRB_MSG_PARTIAL_VERSION_PRESENCE, "Partial "
                                "version xattr presence detected, ignoring "
                                "[GFID: %s]", uuid_utoa (linked_inode->gfid));
        } else {
                need_signing = br_check_object_need_sign (this, xattr, child);
        }

        if (!need_signing)
                goto unref_dict;

        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_TRIGGER_SIGN,
                "Triggering signing for %s [GFID: %s | Brick: %s]",
                loc.path, uuid_utoa (linked_inode->gfid), child->brick_path);
        br_trigger_sign (this, child, linked_inode, &loc, _gf_true);

        ret = 0;

 unref_dict:
        if (xattr)
                dict_unref (xattr);
 unref_inode:
        inode_unref (linked_inode);
 out:
        loc_wipe (&loc);

        return ret;
}

#define BR_CRAWL_THROTTLE_COUNT 50
#define BR_CRAWL_THROTTLE_ZZZ   5

void *
br_oneshot_signer (void *arg)
{
        loc_t       loc   = {0,};
        xlator_t   *this  = NULL;
        br_child_t *child = NULL;

        child = arg;
        this = child->this;

        THIS = this;

        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_CRAWLING_START,
                "Crawling brick [%s], scanning for unsigned objects",
                child->brick_path);

        loc.inode = child->table->root;
        (void) syncop_ftw_throttle
                         (child->xl, &loc,
                         GF_CLIENT_PID_BITD, child, bitd_oneshot_crawl,
                         BR_CRAWL_THROTTLE_COUNT, BR_CRAWL_THROTTLE_ZZZ);

        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_CRAWLING_FINISH,
                "Completed crawling brick [%s]", child->brick_path);

        return NULL;
}

static void
br_set_child_state (br_child_t *child, br_child_state_t state)
{
        pthread_mutex_lock (&child->lock);
        {
                _br_set_child_state (child, state);
        }
        pthread_mutex_unlock (&child->lock);
}

/**
 * At this point a thread is spawned to crawl the filesystem (in
 * tortoise pace) to sign objects that were not signed in previous run(s).
 * Such objects are identified by examining it's dirtyness and timestamp.
 *
 *    pick object:
 *       signature_is_stale() && (object_timestamp() <= stub_init_time())
 *
 * Also, we register to the changelog library to subscribe for event
 * notifications.
 */
static int32_t
br_enact_signer (xlator_t *this, br_child_t *child, br_stub_init_t *stub)
{
        int32_t ret = 0;
        br_private_t *priv = NULL;
        struct gf_brick_spec *brick = NULL;

        priv = this->private;

        brick = GF_CALLOC (1, sizeof (struct gf_brick_spec),
                           gf_common_mt_gf_brick_spec_t);
        if (!brick)
                goto error_return;

        br_fill_brick_spec (brick, stub->export);
        ret = gf_changelog_register_generic
                         (brick, 1, 1, this->ctx->cmd_args.log_file, -1, this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        BRB_MSG_REGISTER_FAILED, "Register to changelog "
                        "failed");
                goto dealloc;
        }

        child->threadrunning = 0;
        ret = gf_thread_create (&child->thread, NULL, br_oneshot_signer, child);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, BRB_MSG_SPAWN_FAILED,
                        "failed to spawn FS crawler thread");
        else
                child->threadrunning = 1;

        /* it's OK to continue, "old" objects would be signed when modified */
        list_add_tail (&child->list, &priv->signing);
        return 0;

 dealloc:
        GF_FREE (brick);
 error_return:
        return -1;
}

static int32_t
br_launch_scrubber (xlator_t *this, br_child_t *child,
                    struct br_scanfs *fsscan, struct br_scrubber *fsscrub)
{
        int32_t ret = -1;
        br_private_t *priv = NULL;
        struct br_monitor *scrub_monitor = NULL;

        priv = this->private;

        scrub_monitor = &priv->scrub_monitor;
        ret = gf_thread_create (&child->thread, NULL, br_fsscanner, child);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ALERT, 0, BRB_MSG_SPAWN_FAILED,
                        "failed to spawn bitrot scrubber daemon [Brick: %s]",
                        child->brick_path);
                goto error_return;
        }

        /* Signal monitor to kick off state machine*/
        pthread_mutex_lock (&scrub_monitor->mutex);
        {
                if (!scrub_monitor->inited)
                        pthread_cond_signal (&scrub_monitor->cond);
                scrub_monitor->inited = _gf_true;
        }
        pthread_mutex_unlock (&scrub_monitor->mutex);

        /**
         * Everything has been setup.. add this subvolume to scrubbers
         * list.
         */
        pthread_mutex_lock (&fsscrub->mutex);
        {
                list_add_tail (&child->list, &fsscrub->scrublist);
                pthread_cond_broadcast (&fsscrub->cond);
        }
        pthread_mutex_unlock (&fsscrub->mutex);

        return 0;

 error_return:
        return -1;
}

static int32_t
br_enact_scrubber (xlator_t *this, br_child_t *child)
{
        int32_t ret = 0;
        br_private_t *priv = NULL;
        struct br_scanfs *fsscan = NULL;
        struct br_scrubber *fsscrub = NULL;

        priv = this->private;

        fsscan = &child->fsscan;
        fsscrub = &priv->fsscrub;

        /**
         * if this child already witnesses a successful connection earlier
         * there's no need to initialize mutexes, condvars, etc..
         */
        if (_br_child_witnessed_connection (child))
                return br_launch_scrubber (this, child, fsscan, fsscrub);

        LOCK_INIT (&fsscan->entrylock);
        pthread_mutex_init (&fsscan->waitlock, NULL);
        pthread_cond_init (&fsscan->waitcond, NULL);

        fsscan->entries = 0;
        INIT_LIST_HEAD (&fsscan->queued);
        INIT_LIST_HEAD (&fsscan->ready);

        ret = br_launch_scrubber (this, child, fsscan, fsscrub);
        if (ret)
                goto error_return;

        return 0;

 error_return:
        LOCK_DESTROY (&fsscan->entrylock);
        pthread_mutex_destroy (&fsscan->waitlock);
        pthread_cond_destroy (&fsscan->waitcond);

        return -1;
}

static int32_t
br_child_enaction (xlator_t *this, br_child_t *child, br_stub_init_t *stub)
{
        int32_t ret = -1;
        br_private_t *priv = this->private;

        pthread_mutex_lock (&child->lock);
        {
                if (priv->iamscrubber)
                        ret = br_enact_scrubber (this, child);
                else
                        ret = br_enact_signer (this, child, stub);

                if (!ret) {
                        child->witnessed = 1;
                        _br_set_child_state (child, BR_CHILD_STATE_CONNECTED);
                        gf_msg (this->name, GF_LOG_INFO,
                                0, BRB_MSG_CONNECTED_TO_BRICK,
                                "Connected to brick %s..", child->brick_path);
                }
        }
        pthread_mutex_unlock (&child->lock);

        return ret;
}

/**
 * This routine fetches various attributes associated with a child which
 * is basically a subvolume. Attributes include brick path and the stub
 * birth time. This is done by performing a lookup on the root followed
 * by getxattr() on a virtual key. Depending on the configuration, the
 * process either acts as a signer or a scrubber.
 */
int32_t
br_brick_connect (xlator_t *this, br_child_t *child)
{
        int32_t         ret      = -1;
        loc_t           loc      = {0, };
        struct iatt     buf      = {0, };
        struct iatt     parent   = {0, };
        br_stub_init_t *stub     = NULL;
        dict_t         *xattr    = NULL;
        int             op_errno = 0;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        br_child_set_scrub_state (child, _gf_false);
        br_set_child_state (child, BR_CHILD_STATE_INITIALIZING);

        loc.inode = inode_ref (child->table->root);
        gf_uuid_copy (loc.gfid, loc.inode->gfid);
        loc.path = gf_strdup ("/");

        ret = syncop_lookup (child->xl, &loc, &buf, &parent, NULL, NULL);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        BRB_MSG_LOOKUP_FAILED, "lookup on root failed");
                goto wipeloc;
        }

        ret = syncop_getxattr (child->xl, &loc, &xattr,
                               GLUSTERFS_GET_BR_STUB_INIT_TIME, NULL, NULL);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        BRB_MSG_GET_INFO_FAILED, "failed to get stub info");
                goto wipeloc;
        }

        ret = dict_get_ptr (xattr, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                            (void **)&stub);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_GET_INFO_FAILED,
                        "failed to extract stub information");
                goto free_dict;
        }

        memcpy (child->brick_path, stub->export, strlen (stub->export) + 1);
        child->tv.tv_sec = ntohl (stub->timebuf[0]);
        child->tv.tv_usec = ntohl (stub->timebuf[1]);

        ret = br_child_enaction (this, child, stub);

 free_dict:
        dict_unref (xattr);
 wipeloc:
        loc_wipe (&loc);
 out:
        if (ret)
                br_set_child_state (child, BR_CHILD_STATE_CONNFAILED);
        return ret;
}

/* TODO: cleanup signer */
static int32_t
br_cleanup_signer (xlator_t *this, br_child_t *child)
{
        return 0;
}

static int32_t
br_cleanup_scrubber (xlator_t *this, br_child_t *child)
{
        int32_t ret = 0;
        br_private_t *priv = NULL;
        struct br_scrubber *fsscrub = NULL;
        struct br_monitor *scrub_monitor = NULL;

        priv    = this->private;
        fsscrub = &priv->fsscrub;
        scrub_monitor = &priv->scrub_monitor;

        if (_br_is_child_scrub_active (child)) {
                scrub_monitor->active_child_count--;
                br_child_set_scrub_state (child, _gf_false);
        }

        /**
         * 0x0: child (brick) goes out of rotation
         *
         * This is fully safe w.r.t. entries for this child being actively
         * scrubbed. Each of the scrubber thread(s) would finish scrubbing
         * the entry (probably failing due to disconnection) and either
         * putting the entry back into the queue or continuing further.
         * Either way, pending entries for this child's queue need not be
         * drained; entries just sit there in the queued/ready list to be
         * consumed later upon re-connection.
         */
        pthread_mutex_lock (&fsscrub->mutex);
        {
                list_del_init (&child->list);
        }
        pthread_mutex_unlock (&fsscrub->mutex);

        /**
         * 0x1: cleanup scanner thread
         *
         * The pending timer needs to be removed _after_ cleaning up the
         * filesystem scanner (scheduling the next scrub time is not a
         * cancellation point).
         */
        ret = gf_thread_cleanup_xint (child->thread);
        if (ret)
                gf_msg (this->name, GF_LOG_INFO,
                        0, BRB_MSG_SCRUB_THREAD_CLEANUP,
                        "Error cleaning up scanner thread");

        gf_msg (this->name, GF_LOG_INFO,
                0, BRB_MSG_SCRUBBER_CLEANED,
                "Cleaned up scrubber for brick [%s]", child->brick_path);

        return 0;
}

/**
 * OK.. this child has made it's mind to go down the drain. So,
 * let's clean up what it touched. (NOTE: there's no need to clean
 * the inode table, it's just reused taking care of stale inodes)
 */
int32_t
br_brick_disconnect (xlator_t *this, br_child_t *child)
{
        int32_t ret = 0;
        struct br_monitor *scrub_monitor = NULL;
        br_private_t *priv = this->private;

        scrub_monitor = &priv->scrub_monitor;

        /* Lock order should be wakelock and then child lock to
         * dead locks.
         */
        pthread_mutex_lock (&scrub_monitor->wakelock);
        {
                pthread_mutex_lock (&child->lock);
                {
                        if (!_br_is_child_connected (child))
                                goto unblock;

                        /* child is on death row.. */
                        _br_set_child_state (child, BR_CHILD_STATE_DISCONNECTED);

                        if (priv->iamscrubber)
                                ret = br_cleanup_scrubber (this, child);
                        else
                                ret = br_cleanup_signer (this, child);
                }
 unblock:
                pthread_mutex_unlock (&child->lock);
        }
        pthread_mutex_unlock (&scrub_monitor->wakelock);

         return ret;
}

/**
 * This function is executed in a separate thread. The thread gets the
 * brick from where CHILD_UP has received from the queue and gets the
 * information regarding that brick (such as brick path).
 */
void *
br_handle_events (void *arg)
{
        int32_t       ret   = 0;
        xlator_t     *this  = NULL;
        br_private_t *priv  = NULL;
        br_child_t *child = NULL;
        struct br_child_event *childev = NULL;

        this = arg;
        priv = this->private;

        /*
         * Since, this is the topmost xlator, THIS has to be set by bit-rot
         * xlator itself (STACK_WIND wont help in this case). Also it has
         * to be done for each thread that gets spawned. Otherwise, a new
         * thread will get global_xlator's pointer when it does "THIS".
         */
        THIS = this;

        while (1) {
                pthread_mutex_lock (&priv->lock);
                {
                        while (list_empty (&priv->bricks))
                                pthread_cond_wait (&priv->cond, &priv->lock);

                        childev = list_first_entry
                                   (&priv->bricks, struct br_child_event, list);
                        list_del_init (&childev->list);
                }
                pthread_mutex_unlock (&priv->lock);

                child = childev->child;
                ret = childev->call (this, child);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRB_MSG_SUBVOL_CONNECT_FAILED,
                                "callback handler for subvolume [%s] failed",
                                child->xl->name);
                GF_FREE (childev);
        }

        return NULL;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int32_t ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_br_stub_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, BRB_MSG_MEM_ACNT_FAILED,
                        "Memory accounting init failed");
                return ret;
        }

        return ret;
}

static void
_br_qchild_event (xlator_t *this, br_child_t *child, br_child_handler *call)
{
        br_private_t *priv = NULL;
        struct br_child_event *childev = NULL;

        priv = this->private;

        childev = GF_CALLOC (1, sizeof (*childev), gf_br_mt_br_child_event_t);
        if (!childev) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, BRB_MSG_NO_MEMORY,
                        "Event unhandled for child.. [Brick: %s]",
                        child->xl->name);
                return;
        }

        INIT_LIST_HEAD (&childev->list);
        childev->this  = this;
        childev->child = child;
        childev->call  = call;

        list_add_tail (&childev->list, &priv->bricks);
}

int
br_scrubber_status_get (xlator_t *this, dict_t **dict)
{
        int                    ret          = -1;
        br_private_t          *priv         = NULL;
        struct br_scrub_stats *scrub_stats  = NULL;

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("bit-rot", priv, out);

        scrub_stats = &priv->scrub_stat;

        ret = br_get_bad_objects_list (this, dict);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to collect corrupt "
                              "files");
        }

        ret = dict_set_int8 (*dict, "scrub-running",
                             scrub_stats->scrub_running);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed setting scrub_running "
                              "entry to the dictionary");
        }

        ret = dict_set_uint64 (*dict, "scrubbed-files",
                               scrub_stats->scrubbed_files);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to setting scrubbed file "
                              "entry to the dictionary");
        }

        ret = dict_set_uint64 (*dict, "unsigned-files",
                               scrub_stats->unsigned_files);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to set unsigned file count"
                              " entry to the dictionary");
        }

        ret = dict_set_uint64 (*dict, "scrub-duration",
                               scrub_stats->scrub_duration);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to set scrub duration"
                              " entry to the dictionary");
        }

        ret = dict_set_dynstr_with_alloc (*dict, "last-scrub-time",
                                          scrub_stats->last_scrub_time);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to set "
                                      "last scrub time value");
        }

out:
        return ret;
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int           idx    = -1;
        int           ret    = -1;
        xlator_t     *subvol = NULL;
        br_child_t   *child  = NULL;
        br_private_t *priv   = NULL;
        dict_t       *output = NULL;
        va_list       ap;
        struct br_monitor  *scrub_monitor = NULL;

        subvol = (xlator_t *)data;
        priv = this->private;
        scrub_monitor = &priv->scrub_monitor;

        gf_msg_trace (this->name, 0, "Notification received: %d", event);

        idx = br_find_child_index (this, subvol);

        switch (event) {
        case GF_EVENT_CHILD_UP:
                if (idx < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRB_MSG_INVALID_SUBVOL, "Got event %d from "
                                "invalid subvolume", event);
                        goto out;
                }

                pthread_mutex_lock (&priv->lock);
                {
                        child = &priv->children[idx];
                        if (child->child_up == 1)
                                goto unblock_0;
                        priv->up_children++;

                        child->child_up = 1;
                        child->xl = subvol;
                        if (!child->table)
                                child->table = inode_table_new (4096, subvol);

                        _br_qchild_event (this, child, br_brick_connect);
                        pthread_cond_signal (&priv->cond);
                }
        unblock_0:
                pthread_mutex_unlock (&priv->lock);

                if (priv->up_children == priv->child_count)
                        default_notify (this, event, data);
                break;

        case GF_EVENT_CHILD_DOWN:
                if (idx < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRB_MSG_INVALID_SUBVOL_CHILD,
                                "Got event %d from invalid subvolume", event);
                        goto out;
                }

                pthread_mutex_lock (&priv->lock);
                {
                        child = &priv->children[idx];
                        if (child->child_up == 0)
                                goto unblock_1;

                        child->child_up = 0;
                        priv->up_children--;

                        _br_qchild_event (this, child, br_brick_disconnect);
                        pthread_cond_signal (&priv->cond);
                }
        unblock_1:
                pthread_mutex_unlock (&priv->lock);

                if (priv->up_children == 0)
                        default_notify (this, event, data);
                break;

        case GF_EVENT_SCRUB_STATUS:
                gf_msg_debug (this->name, GF_LOG_INFO, "BitRot scrub status "
                              "called");
                va_start (ap, data);
                output = va_arg (ap, dict_t *);
                va_end (ap);

                ret = br_scrubber_status_get (this, &output);
                gf_msg_debug (this->name, 0, "returning %d", ret);
                break;

        case GF_EVENT_SCRUB_ONDEMAND:
                gf_log (this->name, GF_LOG_INFO, "BitRot scrub ondemand "
                              "called");

                if (scrub_monitor->state != BR_SCRUB_STATE_PENDING)
                        return -2;

                /* Needs synchronization with reconfigure thread */
                pthread_mutex_lock (&priv->lock);
                {
                        ret = br_scrub_state_machine (this, _gf_true);
                }
                pthread_mutex_unlock (&priv->lock);

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRB_MSG_RESCHEDULE_SCRUBBER_FAILED,
                                "Could not schedule ondemand scrubbing. "
                                "Scrubbing will continue according to "
                                "old frequency.");
                }
                gf_msg_debug (this->name, 0, "returning %d", ret);
                break;
        default:
                default_notify (this, event, data);
        }

 out:
        return 0;
}

/**
 * Initialize signer specific structures, spawn worker threads.
 */

static void
br_fini_signer (xlator_t *this, br_private_t *priv)
{
        int i = 0;

        for (; i < BR_WORKERS; i++) {
                (void) gf_thread_cleanup_xint (priv->obj_queue->workers[i]);
        }

        pthread_cond_destroy (&priv->object_cond);
}

static int32_t
br_init_signer (xlator_t *this, br_private_t *priv)
{
        int i = 0;
        int32_t ret = -1;

        /* initialize gfchangelog xlator context */
        ret = gf_changelog_init (this);
        if (ret)
                goto out;

        pthread_cond_init (&priv->object_cond, NULL);

        priv->obj_queue = GF_CALLOC (1, sizeof (*priv->obj_queue),
                                     gf_br_mt_br_ob_n_wk_t);
        if (!priv->obj_queue)
                goto cleanup_cond;
        INIT_LIST_HEAD (&priv->obj_queue->objects);

        for (i = 0; i < BR_WORKERS; i++) {
                ret = gf_thread_create (&priv->obj_queue->workers[i], NULL,
                                        br_process_object, this);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                BRB_MSG_SPAWN_FAILED, "thread creation"
                                " failed");
                        ret = -1;
                        goto cleanup_threads;
                }
        }

        return 0;

 cleanup_threads:
        for (i--; i >= 0; i--) {
                (void) gf_thread_cleanup_xint (priv->obj_queue->workers[i]);
        }

        GF_FREE (priv->obj_queue);

 cleanup_cond:
        /* that's explicit */
        pthread_cond_destroy (&priv->object_cond);
 out:
        return -1;
}

/**
 * For signer, only rate limit CPU usage (during hash calculation) when
 * compiled with -DBR_RATE_LIMIT_SIGNER cflags, else let it run full
 * throttle.
 */
static int32_t
br_rate_limit_signer (xlator_t *this, int child_count, int numbricks)
{
        br_private_t *priv = NULL;
        tbf_opspec_t spec = {0,};

        priv = this->private;

        spec.op       = TBF_OP_HASH;
        spec.rate     = 0;
        spec.maxlimit = 0;

/**
 * OK. Most implementations of TBF I've come across generate tokens
 * every second (UML, etc..) and some chose sub-second granularity
 * (blk-iothrottle cgroups). TBF algorithm itself does not enforce
 * any logic for choosing generation interval and it seems pretty
 * logical as one could jack up token count per interval w.r.t.
 * generation rate.
 *
 * Value used here is chosen based on a series of test(s) performed
 * to balance object signing time and not maxing out on all available
 * CPU cores. It's obvious to have seconds granularity and jack up
 * token count per interval, thereby achieving close to similar
 * results. Let's stick to this as it seems to be working fine for
 * the set of ops that are throttled.
 **/
        spec.token_gen_interval = 600000; /* In usec */


#ifdef BR_RATE_LIMIT_SIGNER

        double contribution = 0;
        contribution = ((double)1 - ((double)child_count / (double)numbricks));
        if (contribution == 0)
                contribution = 1;
        spec.rate = BR_HASH_CALC_READ_SIZE * contribution;
        spec.maxlimit = BR_WORKERS * BR_HASH_CALC_READ_SIZE;

#endif

        if (!spec.rate)
                gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_RATE_LIMIT_INFO,
                        "[Rate Limit Info] \"FULL THROTTLE\"");
        else
                gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_RATE_LIMIT_INFO,
                        "[Rate Limit Info] \"tokens/sec (rate): %lu, "
                        "maxlimit: %lu\"", spec.rate, spec.maxlimit);

        priv->tbf = tbf_init (&spec, 1);
        return priv->tbf ? 0 : -1;
}

static int32_t
br_signer_handle_options (xlator_t *this, br_private_t *priv, dict_t *options)
{
        if (options)
                GF_OPTION_RECONF ("expiry-time", priv->expiry_time,
                                  options, uint32, error_return);
        else
                GF_OPTION_INIT ("expiry-time", priv->expiry_time,
                                uint32, error_return);

        return 0;

error_return:
        return -1;
}

static int32_t
br_signer_init (xlator_t *this, br_private_t *priv)
{
        int32_t ret = 0;
        int numbricks = 0;

        GF_OPTION_INIT ("expiry-time", priv->expiry_time, uint32, error_return);
        GF_OPTION_INIT ("brick-count", numbricks, int32, error_return);

        ret = br_rate_limit_signer (this, priv->child_count, numbricks);
        if (ret)
                goto error_return;

        ret = br_init_signer (this, priv);
        if (ret)
                goto cleanup_tbf;

        return 0;

 cleanup_tbf:
        /* cleanup TBF */
 error_return:
        return -1;

}

static void
br_free_scrubber_monitor (xlator_t *this, br_private_t *priv)
{
        struct br_monitor *scrub_monitor = &priv->scrub_monitor;

        if (scrub_monitor->timer) {
                (void) gf_tw_del_timer (priv->timer_wheel, scrub_monitor->timer);

                GF_FREE (scrub_monitor->timer);
                scrub_monitor->timer = NULL;
        }

        (void) gf_thread_cleanup_xint (scrub_monitor->thread);

        /* Clean up cond and mutex variables */
        pthread_mutex_destroy (&scrub_monitor->mutex);
        pthread_cond_destroy (&scrub_monitor->cond);

        pthread_mutex_destroy (&scrub_monitor->wakelock);
        pthread_cond_destroy (&scrub_monitor->wakecond);

        pthread_mutex_destroy (&scrub_monitor->donelock);
        pthread_cond_destroy (&scrub_monitor->donecond);

        LOCK_DESTROY (&scrub_monitor->lock);
}

static void
br_free_children (xlator_t *this, br_private_t *priv, int count)
{
        br_child_t *child = NULL;

        for (--count; count >= 0; count--) {
                child = &priv->children[count];
                mem_pool_destroy (child->timer_pool);
                pthread_mutex_destroy (&child->lock);
        }

        GF_FREE (priv->children);
        priv->children = NULL;
}

static int
br_init_children (xlator_t *this, br_private_t *priv)
{
        int i = 0;
        br_child_t *child = NULL;
        xlator_list_t *trav = NULL;

        priv->child_count = xlator_subvolume_count (this);
        priv->children = GF_CALLOC (priv->child_count, sizeof (*priv->children),
                                    gf_br_mt_br_child_t);
        if (!priv->children)
                goto err;

        trav = this->children;
        while (trav) {
                child = &priv->children[i];

                pthread_mutex_init (&child->lock, NULL);
                child->witnessed = 0;

                br_set_child_state (child, BR_CHILD_STATE_DISCONNECTED);

                child->this = this;
                child->xl = trav->xlator;

                child->timer_pool = mem_pool_new
                                    (struct gf_tw_timer_list,  4096);
                if (!child->timer_pool) {
                        gf_msg (this->name, GF_LOG_ERROR,
                                ENOMEM, BRB_MSG_NO_MEMORY,
                                "failed to allocate mem-pool for timer");
                        errno = ENOMEM;
                        goto freechild;
                }

                INIT_LIST_HEAD (&child->list);

                i++;
                trav = trav->next;
        }

        return 0;

 freechild:
        br_free_children (this, priv, i);
 err:
        return -1;
}

int32_t
init (xlator_t *this)
{
        int32_t       ret  = -1;
	br_private_t *priv = NULL;

	if (!this->children) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRB_MSG_NO_CHILD,
                        "FATAL: no children");
		goto out;
	}

        priv = GF_CALLOC (1, sizeof (*priv), gf_br_mt_br_private_t);
        if (!priv) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, BRB_MSG_NO_MEMORY,
                        "failed to allocate memory (->priv)");
                goto out;
        }

        GF_OPTION_INIT ("scrubber", priv->iamscrubber, bool, out);

        ret = br_init_children (this, priv);
        if (ret)
                goto free_priv;

        pthread_mutex_init (&priv->lock, NULL);
        pthread_cond_init (&priv->cond, NULL);

        INIT_LIST_HEAD (&priv->bricks);
        INIT_LIST_HEAD (&priv->signing);

        priv->timer_wheel = glusterfs_global_timer_wheel (this);
        if (!priv->timer_wheel) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRB_MSG_TIMER_WHEEL_UNAVAILABLE,
                        "global timer wheel unavailable");
                goto cleanup;
        }

	this->private = priv;

        if (!priv->iamscrubber) {
                ret = br_signer_init (this, priv);
                if (!ret)
                        ret = br_signer_handle_options (this, priv, NULL);
        } else {
                ret = br_scrubber_init (this, priv);
                if (!ret)
                        ret = br_scrubber_handle_options (this, priv, NULL);
        }

        if (ret)
                goto cleanup;

        ret = gf_thread_create (&priv->thread, NULL, br_handle_events, this);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        BRB_MSG_SPAWN_FAILED, "thread creation failed");
                ret = -1;
        }

        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_BITROT_LOADED,
                        "bit-rot xlator loaded in \"%s\" mode",
                        (priv->iamscrubber) ? "SCRUBBER" : "SIGNER");
                return 0;
        }

 cleanup:
        (void) pthread_cond_destroy (&priv->cond);
        (void) pthread_mutex_destroy (&priv->lock);

        br_free_children (this, priv, priv->child_count);

 free_priv:
        GF_FREE (priv);
 out:
        this->private = NULL;
        return -1;
}

void
fini (xlator_t *this)
{
	br_private_t *priv = this->private;

        if (!priv)
                return;

        if (!priv->iamscrubber)
                br_fini_signer (this, priv);
        else
                (void) br_free_scrubber_monitor (this, priv);

        br_free_children (this, priv, priv->child_count);

        this->private = NULL;
	GF_FREE (priv);

	return;
}

static void
br_reconfigure_monitor (xlator_t *this)
{
        int32_t ret = 0;

        ret = br_scrub_state_machine (this, _gf_false);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRB_MSG_RESCHEDULE_SCRUBBER_FAILED,
                        "Could not reschedule scrubber for the volume. Scrubbing "
                        "will continue according to old frequency.");
        }
}

static int
br_reconfigure_scrubber (xlator_t *this, dict_t *options)
{
        int32_t       ret   = -1;
        br_private_t *priv  = NULL;

        priv = this->private;

        pthread_mutex_lock (&priv->lock);
        {
                ret = br_scrubber_handle_options (this, priv, options);
        }
        pthread_mutex_unlock (&priv->lock);

        if (ret)
                goto err;

        /* change state for all _up_ subvolume(s) */
        pthread_mutex_lock (&priv->lock);
        {
                br_reconfigure_monitor (this);
        }
        pthread_mutex_unlock (&priv->lock);

 err:
        return ret;
}

static int
br_reconfigure_signer (xlator_t *this, dict_t *options)
{
        br_private_t *priv = this->private;

        return br_signer_handle_options (this, priv, options);
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int ret = 0;
        br_private_t *priv = NULL;

        priv = this->private;

        if (priv->iamscrubber)
                ret = br_reconfigure_scrubber (this, options);
        else
                ret = br_reconfigure_signer (this, options);

        return ret;
}

struct xlator_fops fops;

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key = {"expiry-time"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = SIGNING_TIMEOUT,
          .description = "Waiting time for an object on which it waits "
                         "before it is signed",
        },
        { .key = {"brick-count"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Total number of bricks for the current node for "
                         "all volumes in the trusted storage pool.",
        },
        { .key = {"scrubber"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .description = "option to run as a scrubber",
        },
        { .key = {"scrub-throttle"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Scrub-throttle value is a measure of how fast "
                         "or slow the scrubber scrubs the filesystem for "
                         "volume <VOLNAME>",
        },
        { .key = {"scrub-freq"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "biweekly",
          .description = "Scrub frequency for volume <VOLNAME>",
        },
        { .key = {"scrub-state"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "active",
          .description = "Pause/Resume scrub. Upon resume, scrubber "
                         "continues from where it left off.",
        },
	{ .key  = {NULL} },
};
