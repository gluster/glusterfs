/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <sys/uio.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "compat-errno.h"

#include "bit-rot.h"
#include "bit-rot-scrub.h"
#include <pthread.h>

#include "tw.h"

#define BR_HASH_CALC_READ_SIZE  (128 * 1024)

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

static void
br_free_children (xlator_t *this)
{
        br_private_t *priv = NULL;
        int32_t       i    = 0;
        br_child_t   *child = NULL;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                child = &priv->children[i];
                mem_pool_destroy (child->timer_pool);
                list_del_init (&priv->children[i].list);
        }

        GF_FREE (priv->children);

        priv->children = NULL;
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
static inline br_isignature_t *
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
                                        BITROT_OBJECT_BAD_KEY, NULL,
                                        NULL);
        else if (loc)
                ret = syncop_getxattr (child->xl, loc, &xattr,
                                       BITROT_OBJECT_BAD_KEY, NULL,
                                       NULL);

        if (!ret) {
                gf_log (this->name, GF_LOG_DEBUG, "[GFID: %s] is marked "
                        "corrupted", uuid_utoa (inode->gfid));
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
static inline int32_t
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
static inline int32_t
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
                gf_log (this->name, GF_LOG_ERROR, "failed to create fd for the "
                        "inode %s", uuid_utoa (inode->gfid));
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
        br_tbf_t      *tbf    = NULL;
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
                gf_log (this->name, GF_LOG_ERROR, "readv on %s failed (%s)",
                        uuid_utoa (fd->inode->gfid), strerror (errno));
                ret = -1;
                goto out;
        }

        if (ret == 0)
                goto out;

        for (i = 0; i < count; i++) {
                TBF_THROTTLE_BEGIN (tbf, BR_TBF_OP_HASH, iovec[i].iov_len);
                {
                        SHA256_Update (sha256, (const unsigned char *)
                                       (iovec[i].iov_base), iovec[i].iov_len);
                }
                TBF_THROTTLE_BEGIN (tbf, BR_TBF_OP_HASH, iovec[i].iov_len);
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
                        gf_log (this->name, GF_LOG_ERROR, "reading block with "
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

static inline int32_t
br_object_checksum (unsigned char *md,
                    br_object_t *object, fd_t *fd, struct iatt *iatt)
{
        return br_calculate_obj_checksum (md, object->child, fd,  iatt);
}

static inline int32_t
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
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate memory "
                        "for saving hash of the object %s",
                        uuid_utoa (fd->inode->gfid));
                goto out;
        }

        ret = br_object_checksum (md, object, fd, iatt);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "calculating checksum for "
                        "the object %s failed", uuid_utoa (linked_inode->gfid));
                goto free_signature;
        }

        sign = br_prepare_signature (md, SHA256_DIGEST_LENGTH,
                                     BR_SIGNATURE_TYPE_SHA256, object);
        if (!sign) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the signature "
                        "for the object %s", uuid_utoa (fd->inode->gfid));
                goto free_signature;
        }

        xattr = dict_for_key_value
                (GLUSTERFS_SET_OBJECT_SIGNATURE,
                 (void *)sign, signature_size (SHA256_DIGEST_LENGTH));

        if (!xattr) {
                gf_log (this->name, GF_LOG_ERROR, "dict allocation for signing"
                        " failed for the object %s",
                        uuid_utoa (fd->inode->gfid));
                goto free_isign;
        }

        ret = syncop_fsetxattr (object->child->xl, fd, xattr, 0, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "fsetxattr of signature to "
                        "the object %s failed", uuid_utoa (fd->inode->gfid));
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

static inline int br_object_sign_softerror (int32_t op_errno)
{
        return ((op_errno == ENOENT) || (op_errno == ESTALE)
                || (op_errno == ENODATA));
}

void
br_log_object (xlator_t *this, char *op, uuid_t gfid, int32_t op_errno)
{
        int softerror = br_object_sign_softerror (op_errno);
        gf_log (this->name, (softerror) ? GF_LOG_DEBUG : GF_LOG_ERROR,
                "%s() failed on object %s [reason: %s]",
                op, uuid_utoa (gfid), strerror (op_errno));
}

void
br_log_object_path (xlator_t *this, char *op,
                    const char *path, int32_t op_errno)
{
        int softerror = br_object_sign_softerror (op_errno);
        gf_log (this->name, (softerror) ? GF_LOG_DEBUG : GF_LOG_ERROR,
                "%s() failed on object %s [reason: %s]",
                op, path, strerror (op_errno));
}

/**
 * Sign a given object. This routine runs full throttle. There needs to be
 * some form of priority scheduling and/or read burstness to avoid starving
 * (or kicking) client I/O's.
 */
static inline int32_t br_sign_object (br_object_t *object)
{
        int32_t         ret           = -1;
        inode_t        *linked_inode  = NULL;
        xlator_t       *this          = NULL;
        fd_t           *fd            = NULL;
        struct iatt     iatt          = {0, };
        pid_t           pid           = GF_CLIENT_PID_BITD;

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

        ret = br_object_open (this, object, linked_inode, &fd);
        if (!fd) {
                br_log_object (this, "open", object->gfid, -ret);
                goto unref_inode;
        }

        /**
         * we have an open file descriptor on the object. from here on,
         * do not be generous to file operation errors.
         */
        gf_log (this->name, GF_LOG_DEBUG,
                "Signing object [%s]", uuid_utoa (linked_inode->gfid));

        ret = br_object_read_sign (linked_inode, fd, object, &iatt);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "reading and signing of the "
                        "object %s failed", uuid_utoa (linked_inode->gfid));
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

static inline br_object_t *__br_pick_object (br_private_t *priv)
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "SIGNING FAILURE [%s]",
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

        mem_put (timer);
        return;
}

static inline br_object_t *
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

out:
        return object;
}

static inline struct gf_tw_timer_list *
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

        timer->data     = object;
        timer->expires  = priv->expiry_time;
        timer->function = br_add_object_to_queue;
        gf_tw_add_timer (priv->timer_wheel, timer);

out:
        return timer;
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
        uuid_t gfid = {0,};
        xlator_t                *this   = NULL;
        br_object_t             *object = NULL;
        br_child_t              *child  = NULL;
        int32_t                  flags  = 0;
        struct gf_tw_timer_list *timer  = NULL;

        this = xl;

        GF_VALIDATE_OR_GOTO (this->name, ev, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        GF_ASSERT (ev->ev_type == CHANGELOG_OP_TYPE_BR_RELEASE);
        GF_ASSERT (!gf_uuid_is_null (ev->u.releasebr.gfid));

        gf_uuid_copy (gfid, ev->u.releasebr.gfid);

        gf_log (this->name, GF_LOG_DEBUG,
                "RELEASE EVENT [GFID %s]", uuid_utoa (gfid));

        flags = (int32_t)ntohl (ev->u.releasebr.flags);
        if (flags == O_RDONLY) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Read only fd [GFID: %s], ignoring signing..",
                        uuid_utoa (gfid));
                goto out;
        }

        child = br_get_child_from_brick_path (this, brick);
        if (!child) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the subvolume "
                        "for the brick %s", brick);
                goto out;
        }

        object = br_initialize_object (this, child, ev);
        if (!object) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate "
                        "object memory [GFID: %s]", uuid_utoa (gfid));
                goto out;
        }

        timer = br_initialize_timer (this, object, child, ev);
        if (!timer) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate "
                        "object expiry timer [GFID: %s]", uuid_utoa (gfid));
                goto free_object;
        }

        gf_log (this->name, GF_LOG_DEBUG, "->callback: brick [%s], type [%d]\n",
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

static inline gf_boolean_t
br_time_equal (br_child_t *child, struct timeval *tv)
{
        if ((child->tv.tv_sec == tv->tv_sec) &&
            (child->tv.tv_usec == tv->tv_usec))
                return _gf_true;

        return _gf_false;
}

static inline gf_boolean_t
br_check_object_need_sign (xlator_t *this, dict_t *xattr, br_child_t *child)
{
        int32_t              ret       = -1;
        gf_boolean_t         need_sign = _gf_false;
        struct timeval       tv        = {0,};
        br_isignature_out_t *sign      = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, xattr, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);

        ret = dict_get_ptr (xattr, GLUSTERFS_GET_OBJECT_SIGNATURE,
                            (void **)&sign);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get object signature info");
                goto out;
        }

        tv.tv_sec  = ntohl (sign->time[0]);
        tv.tv_usec = ntohl (sign->time[1]);

        /* Object has been opened and hence dirty. Do not sign it */
        if (sign->stale && !br_time_equal (child, &tv))
                need_sign = _gf_true;

out:
        return need_sign;
}

static inline void
br_trigger_sign (xlator_t *this, br_child_t *child, inode_t *linked_inode,
                 loc_t *loc)
{
        fd_t      *fd = NULL;
        int32_t    ret = -1;

        fd = fd_create (linked_inode, 0);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create fd [GFID %s]",
                        uuid_utoa (linked_inode->gfid));
                goto out;
        }

        ret = syncop_open (child->xl, loc, O_RDWR, fd, NULL, NULL);
	if (ret) {
                br_log_object (this, "open", linked_inode->gfid, -ret);
		fd_unref (fd);
		fd = NULL;
	} else {
		fd_bind (fd);
	}

        if (fd)
                syncop_close (fd);

out:
        return;
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
                        gf_log (this->name, GF_LOG_DEBUG, "%s is not a regular "
                                "file", entry->d_name);
                        ret = 0;
                        goto out;
                }
        }

        loc->parent = inode_ref (parent->inode);
        gf_uuid_copy (loc->pargfid, parent->inode->gfid);

        ret = inode_path (parent->inode, entry->d_name, (char **)&loc->path);
        if (ret < 0 || !loc->path) {
                gf_log (this->name, GF_LOG_ERROR, "inode_path on %s "
                        "(parent: %s) failed", entry->d_name,
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
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s is not a regular file, skipping..", entry->d_name);
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
                gf_log (this->name, GF_LOG_WARNING,
                        "Entry [%s] is marked corrupted.. skipping.", loc.path);
                goto unref_inode;
        }

        ret = syncop_getxattr (child->xl, &loc, &xattr,
                               GLUSTERFS_GET_OBJECT_SIGNATURE, NULL, NULL);
        if (ret < 0) {
                op_errno = -ret;
                br_log_object (this, "getxattr", linked_inode->gfid, op_errno);

                if (op_errno == ENODATA)
                        need_signing = _gf_true;
                if (op_errno == EINVAL)
                        gf_log (this->name, GF_LOG_WARNING, "Partial version "
                                "xattr presence detected, ignoring [GFID: %s]",
                                uuid_utoa (linked_inode->gfid));
        } else {
                need_signing = br_check_object_need_sign (this, xattr, child);
        }

        if (!need_signing)
                goto unref_dict;

        gf_log (this->name, GF_LOG_INFO,
                "Triggering signing for %s [GFID: %s | Brick: %s]",
                loc.path, uuid_utoa (linked_inode->gfid), child->brick_path);
        br_trigger_sign (this, child, linked_inode, &loc);

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

        gf_log (this->name, GF_LOG_INFO, "Crawling brick [%s], scanning "
                "for unsigned objects", child->brick_path);

        loc.inode = child->table->root;
        (void) syncop_ftw_throttle
                         (child->xl, &loc,
                         GF_CLIENT_PID_BITD, child, bitd_oneshot_crawl,
                         BR_CRAWL_THROTTLE_COUNT, BR_CRAWL_THROTTLE_ZZZ);

        gf_log (this->name, GF_LOG_INFO,
                "Completed crawling brick [%s]", child->brick_path);

        return NULL;
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
static inline int32_t
br_enact_signer (xlator_t *this, br_child_t *child, br_stub_init_t *stub)
{
        int32_t ret = 0;
        struct gf_brick_spec *brick = NULL;

        brick = GF_CALLOC (1, sizeof (struct gf_brick_spec),
                           gf_common_mt_gf_brick_spec_t);
        if (!brick)
                goto error_return;

        br_fill_brick_spec (brick, stub->export);
        ret = gf_changelog_register_generic
                         (brick, 1, 1, this->ctx->cmd_args.log_file, -1, this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Register to changelog failed"
                        " [Reason: %s]", strerror (errno));
                goto dealloc;
        }

        child->threadrunning = 0;
        ret = gf_thread_create (&child->thread, NULL, br_oneshot_signer, child);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to spawn FS crawler thread");
        else
                child->threadrunning = 1;

        /* it's OK to continue, "old" objects would be signed when modified */
        list_del_init (&child->list);
        return 0;

 dealloc:
        GF_FREE (brick);
 error_return:
        return -1;
}

static inline int32_t
br_enact_scrubber (xlator_t *this, br_child_t *child)
{
        int32_t ret = 0;
        br_private_t *priv = NULL;
        struct br_scanfs *fsscan = NULL;
        struct br_scrubber *fsscrub = NULL;

        priv = this->private;

        fsscan = &child->fsscan;
        fsscrub = &priv->fsscrub;

        LOCK_INIT (&fsscan->entrylock);
        pthread_mutex_init (&fsscan->waitlock, NULL);
        pthread_cond_init (&fsscan->waitcond, NULL);

        fsscan->entries = 0;
        INIT_LIST_HEAD (&fsscan->queued);
        INIT_LIST_HEAD (&fsscan->ready);

        ret = gf_thread_create (&child->thread, NULL, br_fsscanner, child);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ALERT, "failed to spawn bitrot "
                        "scrubber daemon [Brick: %s]", child->brick_path);
                goto error_return;
        }

        /**
         * Everything has been setup.. add this subvolume to scrubbers
         * list.
         */
        pthread_mutex_lock (&fsscrub->mutex);
        {
                list_move (&child->list, &fsscrub->scrublist);
                pthread_cond_broadcast (&fsscrub->cond);
        }
        pthread_mutex_unlock (&fsscrub->mutex);

        return 0;

 error_return:
        return -1;
}

/**
 * This routine fetches various attributes associated with a child which
 * is basically a subvolume. Attributes include brick path and the stub
 * birth time. This is done by performing a lookup on the root followed
 * by getxattr() on a virtual key. Depending on the configuration, the
 * process either acts as a signer or a scrubber.
 */
static inline int32_t
br_brick_connect (xlator_t *this, br_child_t *child)
{
        int32_t         ret      = -1;
        loc_t           loc      = {0, };
        struct iatt     buf      = {0, };
        struct iatt     parent   = {0, };
        br_stub_init_t *stub     = NULL;
        dict_t         *xattr    = NULL;
        br_private_t   *priv     = NULL;
        int             op_errno = 0;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        priv = this->private;

        loc.inode = inode_ref (child->table->root);
        gf_uuid_copy (loc.gfid, loc.inode->gfid);
        loc.path = gf_strdup ("/");

        ret = syncop_lookup (child->xl, &loc, &buf, &parent, NULL, NULL);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "lookup on root failed "
                        "[Reason: %s]", strerror (op_errno));
                goto wipeloc;
        }

        ret = syncop_getxattr (child->xl, &loc, &xattr,
                               GLUSTERFS_GET_BR_STUB_INIT_TIME, NULL, NULL);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "failed to get stub info "
                        "[Reason: %s]", strerror (op_errno));
                goto wipeloc;
        }

        ret = dict_get_ptr (xattr, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                            (void **)&stub);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to extract stub information");
                goto free_dict;
        }

        memcpy (child->brick_path, stub->export, strlen (stub->export) + 1);
        child->tv.tv_sec = ntohl (stub->timebuf[0]);
        child->tv.tv_usec = ntohl (stub->timebuf[0]);

        if (priv->iamscrubber)
                ret = br_enact_scrubber (this, child);
        else
                ret = br_enact_signer (this, child, stub);

 free_dict:
        dict_unref (xattr);
 wipeloc:
        loc_wipe (&loc);
 out:
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
        xlator_t     *this  = NULL;
        br_private_t *priv  = NULL;
        br_child_t   *child = NULL;
        int32_t       ret   = -1;

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
                        while (list_empty (&priv->bricks)) {
                                pthread_cond_wait (&priv->cond,
                                                   &priv->lock);
                        }

                        child = list_entry (priv->bricks.next, br_child_t,
                                            list);
                        if (child && child->child_up) {
                                ret = br_brick_connect (this, child);
                                if (ret == -1)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "failed to connect to the "
                                                "child (subvolume: %s)",
                                                child->xl->name);

                        }

                }
                pthread_mutex_unlock (&priv->lock);
        }

        return NULL;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int32_t     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_br_stub_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        xlator_t                *subvol = NULL;
        br_private_t            *priv   = NULL;
        int                      idx    = -1;
        br_child_t              *child  = NULL;

        subvol = (xlator_t *)data;
        priv = this->private;

        gf_log (this->name, GF_LOG_TRACE, "Notification received: %d",
                event);

        switch (event) {
        case GF_EVENT_CHILD_UP:
                /* should this be done under lock? or is it ok to do it
                   without lock? */
                idx = br_find_child_index (this, subvol);

                pthread_mutex_lock (&priv->lock);
                {
                        if (idx < 0) {
                                gf_log (this->name, GF_LOG_ERROR, "got child "
                                        "up from invalid subvolume");
                        } else {
                                child = &priv->children[idx];
                                if (child->child_up != 1)
                                        child->child_up = 1;
                                if (!child->xl)
                                        child->xl = subvol;
                                if (!child->table)
                                        child->table = inode_table_new (4096,
                                                                       subvol);
                                priv->up_children++;
                                list_add_tail (&child->list, &priv->bricks);
                                pthread_cond_signal (&priv->cond);
                        }
                }
                pthread_mutex_unlock (&priv->lock);
                break;

        case GF_EVENT_CHILD_MODIFIED:
                idx = br_find_child_index (this, subvol);
                if (idx < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "received child up "
                                "from invalid subvolume");
                        goto out;
                }
                priv = this->private;
                /* ++(priv->generation); */
                break;
        case GF_EVENT_CHILD_DOWN:
                idx = br_find_child_index (this, subvol);
                if (idx < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "received child down "
                                "from invalid subvolume");
                        goto out;
                }

                pthread_mutex_lock (&priv->lock);
                {
                        if (priv->children[idx].child_up == 1) {
                                priv->children[idx].child_up = 0;
                                priv->up_children--;
                        }
                }
                pthread_mutex_unlock (&priv->lock);
                break;
        case GF_EVENT_PARENT_UP:
                default_notify (this, GF_EVENT_PARENT_UP, data);
                break;
        }

out:
        return 0;
}

/**
 * Initialize signer specific structures, spawn worker threads.
 */

static inline void
br_fini_signer (xlator_t *this, br_private_t *priv)
{
        int i = 0;

        for (; i < BR_WORKERS; i++) {
                (void) gf_thread_cleanup_xint (priv->obj_queue->workers[i]);
        }

        pthread_cond_destroy (&priv->object_cond);
}

static inline int32_t
br_init_signer (xlator_t *this, br_private_t *priv)
{
        int i = 0;
        int32_t ret = -1;

        /* initialize gfchangelog xlator context */
        ret = gf_changelog_init (this);
        if (ret)
                goto out;

        priv->timer_wheel = glusterfs_global_timer_wheel (this);
        if (!priv->timer_wheel) {
                gf_log (this->name, GF_LOG_ERROR,
                        "global timer wheel unavailable");
                goto out;
        }

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
                        gf_log (this->name, GF_LOG_ERROR,
                                "thread creation failed (%s)", strerror (-ret));
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
        br_tbf_opspec_t spec = {0,};

        priv = this->private;

        spec.op       = BR_TBF_OP_HASH;
        spec.rate     = 0;
        spec.maxlimit = 0;

#ifdef BR_RATE_LIMIT_SIGNER

        double contribution = 0;
        contribution = ((double)1 - ((double)child_count / (double)numbricks));
        if (contribution == 0)
                contribution = 1;
        spec.rate = BR_HASH_CALC_READ_SIZE * contribution;
        spec.maxlimit = BR_WORKERS * BR_HASH_CALC_READ_SIZE;

#endif

        if (!spec.rate)
                gf_log (this->name,
                GF_LOG_INFO, "[Rate Limit Info] \"FULL THROTTLE\"");
        else
                gf_log (this->name, GF_LOG_INFO,
                        "[Rate Limit Info] \"tokens/sec (rate): %lu, "
                        "maxlimit: %lu\"", spec.rate, spec.maxlimit);

        priv->tbf = br_tbf_init (&spec, 1);
        return priv->tbf ? 0 : -1;
}

static int32_t
br_signer_init (xlator_t *this, br_private_t *priv)
{
        int32_t ret = 0;
        int numbricks = 0;

        GF_OPTION_INIT ("expiry-time", priv->expiry_time, int32, error_return);
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

int32_t
init (xlator_t *this)
{
        int            i    = 0;
        int32_t        ret  = -1;
	br_private_t  *priv = NULL;
        xlator_list_t *trav = NULL;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR, "FATAL: no children");
		goto out;
	}

        priv = GF_CALLOC (1, sizeof (*priv), gf_br_mt_br_private_t);
        if (!priv) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to allocate memory (->priv)");
                goto out;
        }

        GF_OPTION_INIT ("scrubber", priv->iamscrubber, bool, out);

        priv->child_count = xlator_subvolume_count (this);
        priv->children = GF_CALLOC (priv->child_count, sizeof (*priv->children),
                                    gf_br_mt_br_child_t);
        if (!priv->children)
                goto free_priv;

        trav = this->children;
        while (trav) {
                priv->children[i].this = this;
                priv->children[i].xl = trav->xlator;

                priv->children[i].timer_pool =
                                  mem_pool_new (struct gf_tw_timer_list,  4096);
                if (!priv->children[i].timer_pool) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to allocate mem-pool for timer");
                        errno = ENOMEM;
                        goto free_children;
                }

                i++;
                trav = trav->next;
        }

        pthread_mutex_init (&priv->lock, NULL);
        pthread_cond_init (&priv->cond, NULL);

        for (i = 0; i < priv->child_count; i++)
                INIT_LIST_HEAD (&priv->children[i].list);
        INIT_LIST_HEAD (&priv->bricks);

	this->private = priv;

        if (!priv->iamscrubber) {
                ret = br_signer_init (this, priv);
        } else {
                ret = br_scrubber_init (this, priv);
                if (!ret)
                        ret = br_scrubber_handle_options (this, priv, NULL);
        }

        if (ret)
                goto cleanup_mutex;

        ret = gf_thread_create (&priv->thread, NULL, br_handle_events, this);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "thread creation failed (%s)", strerror (-ret));
                ret = -1;
        }

        if (!ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "bit-rot xlator loaded in \"%s\" mode",
                        (priv->iamscrubber) ? "SCRUBBER" : "SIGNER");
                return 0;
        }

 cleanup_mutex:
        (void) pthread_cond_destroy (&priv->cond);
        (void) pthread_mutex_destroy (&priv->lock);
 free_children:
        for (i = 0; i < priv->child_count; i++) {
                if (priv->children[i].timer_pool)
                        mem_pool_destroy (priv->children[i].timer_pool);
        }

        GF_FREE (priv->children);
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
        br_free_children (this);

        this->private = NULL;
	GF_FREE (priv);

	return;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        br_private_t *priv = this->private;

        if (!priv->iamscrubber)
                return 0;

        return br_scrubber_handle_options (this, priv, options);
}

struct xlator_fops fops;

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key = {"expiry-time"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "120",
          .description = "default time duration for which an object waits "
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
