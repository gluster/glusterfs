/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "trash.h"
#include "trash-mem-types.h"
#include "syscall.h"

#define root_gfid        (uuid_t){0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
#define trash_gfid       (uuid_t){0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5}
#define internal_op_gfid (uuid_t){0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6}

int32_t
trash_truncate_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *prebuf, struct iatt *postbuf,
                           dict_t *xdata);

int32_t
trash_truncate_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *stbuf, struct iatt *preparent,
                          struct iatt *postparent, dict_t *xdata);

int32_t
trash_unlink_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent,
                         dict_t *xdata);

/* Common routines used in this translator */

/**
 * When a directory/file is created under trash directory, it should have
 * the same permission as before. This function will fetch permission from
 * the existing directory and returns the same
 */
mode_t
get_permission (char *path)
{
        mode_t                  mode                    = 0755;
        struct stat             sbuf                    = {0,};
        struct iatt             ibuf                    = {0,};
        int                     ret                     = 0;

        ret = sys_stat (path, &sbuf);
        if (!ret) {
                iatt_from_stat (&ibuf, &sbuf);
                mode = st_mode_from_ia (ibuf.ia_prot, ibuf.ia_type);
        } else
                gf_log ("trash", GF_LOG_DEBUG, "stat on %s failed"
                                " using default", path);
        return mode;
}

/**
 * For normalization, trash directory name is stored inside priv structure as
 * '/trash_directory/'. As a result the trailing and leading slashes are being
 * striped out for additional usage.
 */
int
extract_trash_directory (char *priv_value, const char **trash_directory)
{
        char                    *tmp                    = NULL;
        int                     ret                     = 0;

        GF_VALIDATE_OR_GOTO("trash", priv_value, out);

        tmp = gf_strdup (priv_value + 1);
        if (!tmp) {
                ret = ENOMEM;
                goto out;
        }
        if (tmp[strlen(tmp)-1] == '/')
                tmp[strlen(tmp)-1] = '\0';
        *trash_directory = gf_strdup (tmp);
        if (!(*trash_directory)) {
                ret = ENOMEM;
                goto out;
        }
out:
        if (tmp)
                GF_FREE (tmp);
        return ret;
}

/**
 * The trash directory path should be append at begining of file path for
 * delete or truncate operations. Normal trashing moves the contents to
 * trash directory and trashing done by internal operations are moved to
 * internal_op directory inside trash.
 */
void
copy_trash_path (const char *priv_value, gf_boolean_t internal, char *path)
{
        char                    trash_path[PATH_MAX]    = {0,};

        strcpy (trash_path, priv_value);
        if (internal)
                strcat (trash_path, "internal_op/");

        strcpy (path, trash_path);
}

/**
 * This function performs the reverse operation of copy_trash_path(). It gives
 * out a pointer, whose starting value will be the path inside trash directory,
 * similar to orginal path.
 */
void
remove_trash_path (const char *path, gf_boolean_t internal, char **rem_path)
{
        if (rem_path == NULL) {
                return;
        }

        *rem_path =  strchr (path + 1, '/');
        if (internal)
                *rem_path =  strchr (*rem_path + 1, '/');
}


/**
 * Checks whether the given path reside under the specified eliminate path
 */
int
check_whether_eliminate_path (trash_elim_path *trav, const char *path)
{
        int                     match                           = 0;

        while (trav) {
                if (strncmp (path, trav->path, strlen(trav->path)) == 0) {
                        match++;
                        break;
                }
                trav = trav->next;
        }
        return match;
}

/**
 * Stores the eliminate path into internal eliminate path structure
 */
int
store_eliminate_path (char *str, trash_elim_path **eliminate)
{
        trash_elim_path         *trav                   = NULL;
        char                    *component              = NULL;
        char                    elm_path[PATH_MAX]      = {0,};
        int                     ret                     = 0;
        char                    *strtokptr              = NULL;

        if (eliminate == NULL) {
                ret = EINVAL;
                goto out;
        }

        component = strtok_r (str, ",", &strtokptr);
        while (component) {
                trav = GF_CALLOC (1, sizeof (*trav),
                                 gf_trash_mt_trash_elim_path);
                if (!trav) {
                        ret = ENOMEM;
                        goto out;
                }
                if (component[0] == '/')
                        sprintf(elm_path, "%s", component);
                else
                        sprintf(elm_path, "/%s", component);

                if (component[strlen(component)-1] != '/')
                        strcat (elm_path, "/");

                trav->path = gf_strdup(elm_path);
                if (!trav->path) {
                                ret = ENOMEM;
                                gf_log ("trash", GF_LOG_DEBUG, "out of memory");
                                goto out;
                }
                trav->next = *eliminate;
                *eliminate = trav;
                component = strtok_r (NULL, ",", &strtokptr);
        }
out:
        return ret;
}

/**
 * Appends time stamp to given string
 */
void
append_time_stamp (char *name)
{
        int                     i;
        char                    timestr[64]            = {0,};

        gf_time_fmt (timestr, sizeof(timestr), time (NULL),
                     gf_timefmt_F_HMS);

        /* removing white spaces in timestamp */
        for (i = 0; i < strlen (timestr); i++) {
                if (timestr[i] == ' ')
                        timestr[i] = '_';
        }
        strcat (name, "_");
        strcat (name, timestr);
}

/* *
 * Check whether delete/rename operation is permitted on
 * trash directory
 */

gf_boolean_t
check_whether_op_permitted (trash_private_t *priv, loc_t *loc)
{
        if ((priv->state &&
            (gf_uuid_compare(loc->inode->gfid, trash_gfid) == 0)))
                return _gf_false;
        if (priv->internal &&
           (gf_uuid_compare(loc->inode->gfid, internal_op_gfid) == 0))
                return _gf_false;

        return _gf_true;
}

/**
 * Wipe the memory used by trash location variable
 */
void
trash_local_wipe (trash_local_t *local)
{
        if (!local)
                goto out;

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);

        if (local->fd)
                fd_unref (local->fd);
        if (local->newfd)
                fd_unref (local->newfd);

        mem_put (local);
out:
        return;
}

/**
 * Wipe the memory used by eliminate path through a
 * recursive call
 */
void
wipe_eliminate_path (trash_elim_path **trav)
{
        if (trav == NULL) {
                return;
        }

        if (*trav == NULL) {
                return;
        }

        wipe_eliminate_path (&(*trav)->next);
        GF_FREE ((*trav)->path);
        GF_FREE (*trav);
        *trav = NULL;
}

/**
 * This is the call back of rename fop initated using STACK_WIND in
 * reconfigure/notify function which is used to rename trash directory
 * in the brick when it is required either in volume start or set.
 * This frame  must destroyed from this function itself since it was
 * created by trash xlator
 */
int32_t
trash_dir_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      dict_t *xdata)
{
        trash_private_t       *priv      = NULL;
        trash_local_t         *local     = NULL;

        priv = this->private;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "rename trash directory "
                                "failed: %s", strerror (op_errno));
                goto out;
        }

        GF_FREE (priv->oldtrash_dir);

        priv->oldtrash_dir = gf_strdup(priv->newtrash_dir);
        if (!priv->oldtrash_dir) {
                op_ret = ENOMEM;
                gf_log (this->name, GF_LOG_DEBUG,
                                "out of memory");
        }

out:
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);
        return op_ret;
}

int
rename_trash_directory (xlator_t *this)
{
        trash_private_t       *priv      = NULL;
        int                   ret        = 0;
        loc_t                 loc        = {0, };
        loc_t                 old_loc    = {0, };
        call_frame_t          *frame     = NULL;
        trash_local_t         *local     = NULL;

        priv = this->private;

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                                "failed to create frame");
                ret = ENOMEM;
                goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }
        frame->local = local;

        /* assign new location values to new_loc members */
        gf_uuid_copy (loc.gfid, trash_gfid);
        gf_uuid_copy (loc.pargfid, root_gfid);
        ret = extract_trash_directory (priv->newtrash_dir,
                                                &loc.name);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                                "out of memory");
                goto out;
        }
        loc.path = gf_strdup (priv->newtrash_dir);
        if (!loc.path) {
                ret = ENOMEM;
                gf_log (this->name, GF_LOG_DEBUG,
                                "out of memory");
                goto out;
        }

        /* assign old location values to old_loc members */
        gf_uuid_copy (old_loc.gfid, trash_gfid);
        gf_uuid_copy (old_loc.pargfid, root_gfid);
        ret = extract_trash_directory (priv->oldtrash_dir,
                                                &old_loc.name);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                                "out of memory");
                goto out;
        }
        old_loc.path = gf_strdup (priv->oldtrash_dir);
        if (!old_loc.path) {
                ret = ENOMEM;
                gf_log (this->name, GF_LOG_DEBUG,
                                "out of memory");
                goto out;
        }

        old_loc.inode = inode_ref (priv->trash_inode);
        gf_uuid_copy(old_loc.inode->gfid, old_loc.gfid);

        loc_copy (&local->loc, &old_loc);
        loc_copy (&local->newloc, &loc);

        STACK_WIND (frame, trash_dir_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    &old_loc, &loc, NULL);
        return 0;

out:
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);

        return ret;
}

int32_t
trash_internal_op_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
       trash_local_t          *local   = NULL;
       local = frame->local;

        if (op_ret != 0 && !(op_errno == EEXIST))
                gf_log (this->name, GF_LOG_ERROR, "mkdir failed for "
                        "internal op directory : %s", strerror (op_errno));

        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);
        return op_ret;
}

/**
 * This is the call back of mkdir fop initated using STACK_WIND in
 * notify/reconfigure function which is used to create trash directory
 * in the brick when "trash" is on. The frame of the mkdir must
 * destroyed from this function itself since it was created by trash xlator
 */

int32_t
trash_dir_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        trash_private_t        *priv    = NULL;
        trash_local_t          *local   = NULL;

        priv = this->private;

        local = frame->local;

        if (op_ret == 0) {
                priv->oldtrash_dir = gf_strdup (priv->newtrash_dir);
                if (!priv->oldtrash_dir) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        op_ret = ENOMEM;
                }
        } else if (op_ret != 0 && errno != EEXIST)
                gf_log (this->name, GF_LOG_ERROR, "mkdir failed for trash"
                                " directory : %s", strerror (op_errno));

        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);
        return op_ret;
}

/**
 * This getxattr calls returns existing trash directory path in
 * the dictionary
 */
int32_t
trash_dir_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
        data_t                 *data                  = NULL;
        trash_private_t        *priv                  = NULL;
        int                    ret                    = 0;
        trash_local_t          *local                 = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;

        data = dict_get (dict, GET_ANCESTRY_PATH_KEY);
        if (!data) {
                goto out;
        }
        priv->oldtrash_dir = GF_CALLOC (1, PATH_MAX,
                                        gf_common_mt_char);
        if (!priv->oldtrash_dir) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                 ret = ENOMEM;
                 goto out;
        }
        /* appending '/' if it is not present */
        sprintf (priv->oldtrash_dir, "%s%c", data->data,
                 data->data[strlen(data->data) - 1] != '/' ? '/' : '\0'
                 );
        gf_log (this->name, GF_LOG_DEBUG, "old trash directory path "
                                          "is %s", priv->oldtrash_dir);
        if (strcmp(priv->newtrash_dir, priv->oldtrash_dir) != 0) {

                /* When user set a new name for trash directory, trash
                * xlator will perform a rename operation on old trash
                * directory to the new one using a STACK_WIND from here.
                * This option can be configured only when volume is in
                * started state
                */
                ret = rename_trash_directory (this);
        }

out:
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);
        return ret;
}
/**
 * This is a nameless look up for internal op directory
 * The lookup is based on gfid, because internal op directory
 * has fixed gfid.
 */
int32_t
trash_internalop_dir_lookup_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, inode_t *inode,
                                 struct iatt *buf, dict_t *xdata,
                                 struct iatt *postparent)
{
        trash_private_t       *priv      = NULL;
        int                   ret        = 0;
        uuid_t                *gfid_ptr  = NULL;
        loc_t                 loc        = {0, };
        char                  internal_op_path[PATH_MAX]        = {0,};
        dict_t                *dict      = NULL;
        trash_local_t          *local    = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;
        if (op_ret != 0 && op_errno == ENOENT) {
                loc_wipe (&local->loc);
                gfid_ptr = GF_CALLOC (1, sizeof(uuid_t),
                                   gf_common_mt_uuid_t);
                if (!gfid_ptr) {
                        ret = ENOMEM;
                        goto out;
                }

                gf_uuid_copy (*gfid_ptr, internal_op_gfid);

                dict = dict_new ();
                if (!dict) {
                        ret = ENOMEM;
                        goto out;
                }
                ret = dict_set_dynptr (dict, "gfid-req", gfid_ptr,
                               sizeof (uuid_t));
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "setting key gfid-req failed");
                        goto out;
                }
                gf_uuid_copy (loc.gfid, internal_op_gfid);
                gf_uuid_copy (loc.pargfid, trash_gfid);

                loc.inode = inode_new (priv->trash_itable);

                /* The mkdir call for creating internal op directory */
                loc.name = gf_strdup ("internal_op");
                if (!loc.name) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                 "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                sprintf (internal_op_path, "%s%s/",
                         priv->newtrash_dir, loc.name);

                loc.path = gf_strdup (internal_op_path);
                if (!loc.path) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                 "out of memory");
                        ret = ENOMEM;
                        goto out;
                }

                loc_copy (&local->loc, &loc);
                STACK_WIND (frame, trash_internal_op_mkdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir,
                            &loc, 0755, 0022, dict);
                return 0;
        }

out:
        if (ret && gfid_ptr)
                GF_FREE (gfid_ptr);
        if (dict)
                dict_unref (dict);
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);
        return op_ret;
}


/**
 * This is a nameless look up for old trash directory
 * The lookup is based on gfid, because trash directory
 * has fixed gfid.
 */
int32_t
trash_dir_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, inode_t *inode,
                      struct iatt *buf, dict_t *xdata,
                      struct iatt *postparent)
{
        trash_private_t         *priv                  = NULL;
        loc_t                   loc                    = {0,};
        int                     ret                    = 0;
        uuid_t                  *gfid_ptr              = NULL;
        dict_t                  *dict                  = NULL;
        trash_local_t           *local                 = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;

        loc_wipe (&local->loc);
        if (op_ret == 0) {

                gf_log (this->name, GF_LOG_DEBUG, "inode found with gfid %s",
                                   uuid_utoa(buf->ia_gfid));

                gf_uuid_copy (loc.gfid,  trash_gfid);

                /* Find trash inode using available information */
                priv->trash_inode = inode_link (inode, NULL, NULL, buf);

                loc.inode = inode_ref (priv->trash_inode);
                loc_copy (&local->loc, &loc);

                /*Used to find path of old trash directory*/
                STACK_WIND (frame, trash_dir_getxattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->getxattr, &loc,
                            GET_ANCESTRY_PATH_KEY, xdata);
                return 0;
        }

        /* If there is no old trash directory we set its value to new one,
         * which is the valid condition for trash directory creation
         */
        else {
                gf_log (this->name, GF_LOG_DEBUG, "Creating trash "
                                   "directory %s ",
                                   priv->newtrash_dir);

                gfid_ptr = GF_CALLOC (1, sizeof(uuid_t),
                                          gf_common_mt_uuid_t);
                if (!gfid_ptr) {
                        ret = ENOMEM;
                        goto out;
                }
                gf_uuid_copy (*gfid_ptr, trash_gfid);

                gf_uuid_copy (loc.gfid, trash_gfid);
                gf_uuid_copy (loc.pargfid, root_gfid);
                ret = extract_trash_directory (priv->newtrash_dir,
                                               &loc.name);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                        "out of memory");
                        goto out;
                }
                loc.path = gf_strdup (priv->newtrash_dir);
                if (!loc.path) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                        "out of memory");
                        ret = ENOMEM;
                        goto out;
                }

                priv->trash_inode = inode_new (priv->trash_itable);
                priv->trash_inode->ia_type = IA_IFDIR;
                loc.inode = inode_ref (priv->trash_inode);
                dict = dict_new ();
                if (!dict) {
                        ret = ENOMEM;
                        goto out;
                }
                /* Fixed gfid is set for trash directory with
                 * this function
                 */
                ret = dict_set_dynptr (dict, "gfid-req", gfid_ptr,
                                      sizeof (uuid_t));
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "setting key gfid-req failed");
                        goto out;
                }
                loc_copy (&local->loc, &loc);

                /* The mkdir call for creating trash directory */
                STACK_WIND (frame, trash_dir_mkdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir, &loc, 0755,
                            0022, dict);
                return 0;
        }
out:
        if (ret && gfid_ptr)
                GF_FREE (gfid_ptr);
        if (dict)
                dict_unref (dict);
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        trash_local_wipe (local);
        return ret;
}

int
create_or_rename_trash_directory (xlator_t *this)
{
        trash_private_t       *priv      = NULL;
        int                   ret        = 0;
        loc_t                 loc        = {0, };
        call_frame_t          *frame     = NULL;
        trash_local_t         *local     = NULL;

        priv = this->private;


        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                                "failed to create frame");
                ret = ENOMEM;
                goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }
        frame->local = local;

        loc.inode = inode_new (priv->trash_itable);
        gf_uuid_copy (loc.gfid, trash_gfid);
        loc_copy (&local->loc, &loc);
        gf_log (this->name, GF_LOG_DEBUG, "nameless lookup for"
                           "old trash directory");
        STACK_WIND (frame, trash_dir_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    &loc, NULL);
out:
        return ret;
}

int
create_internalop_directory (xlator_t *this)
{
        trash_private_t       *priv      = NULL;
        int                   ret        = 0;
        loc_t                 loc        = {0, };
        call_frame_t          *frame     = NULL;
        trash_local_t         *local     = NULL;

        priv = this->private;

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                                "failed to create frame");
                ret = ENOMEM;
                goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }
        frame->local = local;

        gf_uuid_copy (loc.gfid, internal_op_gfid);
        gf_uuid_copy (loc.pargfid, trash_gfid);
        loc.inode = inode_new (priv->trash_itable);
        loc.inode->ia_type = IA_IFDIR;

        loc_copy (&local->loc, &loc);
        STACK_WIND (frame, trash_internalop_dir_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    &loc, NULL);
out:

        return ret;
}

int32_t
trash_common_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode,
                             buf, preparent, postparent, xdata);
        return 0;
}

int32_t
trash_common_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent, xdata);
        return 0;
}

int32_t
trash_common_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent,
                   dict_t *xdata)
{
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        return 0;
}

/**
 * move backs from trash translator to unlink call
 */
int32_t
trash_common_unwind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         struct iatt *preparent, struct iatt *postparent,
                         dict_t *xdata)
{
        TRASH_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent,
                            postparent, xdata);
        return 0;
}

/**
 * If the path is not present in the trash directory,it will recursively
 * call this call-back and one by one directories will be created from
 * the starting
 */
int32_t
trash_unlink_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *stbuf, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
        trash_local_t       *local              = NULL;
        char                *tmp_str            = NULL;
        char                *tmp_path           = NULL;
        char                *tmp_dirname        = NULL;
        char                *tmp_stat           = NULL;
        char                real_path[PATH_MAX] = {0,};
        char                *dir_name           = NULL;
        size_t              count               = 0;
        int32_t             loop_count          = 0;
        int                 i                   = 0;
        loc_t               tmp_loc             = {0,};
        trash_private_t     *priv               = NULL;
        int                 ret                 = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local   = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        TRASH_UNSET_PID (frame, local);

        tmp_str = gf_strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = -1;
                goto out;
        }
        loop_count = local->loop_count;

        /* The directory is not present , need to create it */
        if ((op_ret == -1) &&  (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;
                        i++;
                        if (i > loop_count)
                                break;
                        tmp_dirname = strchr (tmp_str + count + 1, '/');
                }
                tmp_path = gf_memdup (local->newpath, count + 1);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                tmp_path[count] = '\0';

                loc_copy (&tmp_loc, &local->loc);
                tmp_loc.path = gf_strdup (tmp_path);
                if (!tmp_loc.path) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }

                /* Stores the the name of directory to be created */
                tmp_loc.name = gf_strdup (strrchr(tmp_path, '/') + 1);
                if (!tmp_loc.name) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                strcpy (real_path, priv->brick_path);
                remove_trash_path (tmp_path, (frame->root->pid < 0), &tmp_stat);
                if (tmp_stat)
                        strcat (real_path, tmp_stat);

                TRASH_SET_PID (frame, local);

                STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_path,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, get_permission(real_path),
                                   0022, xdata);
                loc_wipe (&tmp_loc);
                goto out;
        }

        /* Given path is created , comparing to the required path */
        if (op_ret == 0) {
                dir_name = dirname (tmp_str);
                if (strcmp((char *)cookie, dir_name) == 0) {
                        /* File path exists we can rename it*/
                        loc_copy (&tmp_loc, &local->loc);
                        tmp_loc.path = local->newpath;
                        STACK_WIND (frame, trash_unlink_rename_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->rename,
                                    &local->loc, &tmp_loc, xdata);
                        goto out;
                }
        }

        if ((op_ret == -1) && (op_errno != EEXIST)) {
                gf_log (this->name, GF_LOG_ERROR, "Directory creation failed [%s]. "
                                "Therefore unlinking %s without moving to trash "
                                "directory", strerror(op_errno), local->loc.name);
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->loc, 0,
                            xdata);
                goto out;
        }

        LOCK (&frame->lock);
        {
                loop_count = ++local->loop_count;
        }
        UNLOCK (&frame->lock);

        tmp_dirname = strchr (tmp_str, '/');

        /* Path is not completed , need to create remaining path */
        while (tmp_dirname) {
                count = tmp_dirname - tmp_str;
                if (count == 0)
                        count = 1;
                i++;
                if (i > loop_count)
                        break;
                tmp_dirname = strchr (tmp_str + count + 1, '/');
        }
        tmp_path = gf_memdup (local->newpath, count + 1);
        if (!tmp_path) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = -1;
                goto out;
        }
        tmp_path[count] = '\0';

        loc_copy (&tmp_loc, &local->loc);
        tmp_loc.path = gf_strdup (tmp_path);
        if (!tmp_loc.path) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = -1;
                goto out;
        }

        /* Stores the the name of directory to be created */
        tmp_loc.name = gf_strdup (strrchr(tmp_path, '/') + 1);
        if (!tmp_loc.name) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = -1;
                goto out;
        }

        strcpy (real_path, priv->brick_path);
        remove_trash_path (tmp_path, (frame->root->pid < 0), &tmp_stat);
        if (tmp_stat)
                strcat (real_path, tmp_stat);

        TRASH_SET_PID (frame, local);

        STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_path,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->mkdir, &tmp_loc,
                           get_permission(real_path), 0022, xdata);

out:
        if (tmp_path)
                GF_FREE (tmp_path);
        if (tmp_str)
                GF_FREE (tmp_str);
        return ret;
}

/**
 * The name of unlinking file should be renamed as starting
 * from trash directory as mentioned in the mount point
 */
int32_t
trash_unlink_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent,
                         dict_t *xdata)
{
        trash_local_t      *local              = NULL;
        trash_private_t     *priv              = NULL;
        char               *tmp_str            = NULL;
        char               *dir_name           = NULL;
        char               *tmp_cookie         = NULL;
        loc_t              tmp_loc             = {0,};
        dict_t             *new_xdata          = NULL;
        char               *tmp_stat           = NULL;
        char               real_path[PATH_MAX] = {0,};
        int                ret                 = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                /* the file path doesnot exists we want to create path
                 * for the file
                 */
                tmp_str = gf_strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                dir_name = dirname (tmp_str); /* stores directory name */

                loc_copy (&tmp_loc, &local->loc);
                tmp_loc.path = gf_strdup (dir_name);
                if (!tmp_loc.path) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }

                tmp_cookie = gf_strdup (dir_name);
                if (!tmp_cookie) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                strcpy (real_path, priv->brick_path);
                remove_trash_path (tmp_str, (frame->root->pid < 0), &tmp_stat);
                if (tmp_stat)
                        strcat (real_path, tmp_stat);

                TRASH_SET_PID (frame, local);

                /* create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_cookie,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, get_permission(real_path),
                                   0022, xdata);
                loc_wipe (&tmp_loc);
                goto out;
        }

        if ((op_ret == -1) && (op_errno == ENOTDIR)) {
                /* if entry is already present in trash directory,
                 * new one is not copied*/
                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists, cannot keep the copy, deleting",
                        local->newpath);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            &local->loc, 0, xdata);

                goto out;
        }

        if ((op_ret == -1) && (op_errno == EISDIR)) {

                /* if entry is directory,we remove directly */
                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists as directory, cannot keep copy, "
                        "deleting", local->newpath);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            &local->loc, 0, xdata);
                goto out;
        }

        /**********************************************************************
         *
         * CTR Xlator message handling done here!
         *
         **********************************************************************/
        /**
         * If unlink is handled by trash translator, it should inform the
         * CTR Xlator. And trash translator only handles the unlink for
         * the last hardlink.
         *
         * Check if there is a GF_REQUEST_LINK_COUNT_XDATA from CTR Xlator
         *
         */

        if (local->ctr_link_count_req) {

                /* Sending back inode link count to ctr_unlink
                 * (changetimerecoder xlator) via
                 * "GF_RESPONSE_LINK_COUNT_XDATA" key using xdata.
                 * */
                if (xdata) {
                        ret = dict_set_uint32 (xdata,
                                               GF_RESPONSE_LINK_COUNT_XDATA,
                                               1);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set"
                                        " GF_RESPONSE_LINK_COUNT_XDATA");
                        }
                } else {
                        new_xdata = dict_new ();
                        if (!new_xdata) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Memory allocation failure while "
                                        "creating new_xdata");
                                goto ctr_out;
                        }
                        ret = dict_set_uint32 (new_xdata,
                                               GF_RESPONSE_LINK_COUNT_XDATA,
                                               1);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set"
                                        " GF_RESPONSE_LINK_COUNT_XDATA");
                        }
ctr_out:
                        TRASH_STACK_UNWIND (unlink, frame, 0, op_errno,
                                            preoldparent, postoldparent,
                                            new_xdata);
                        goto out;
                }
         }
        /* All other cases, unlink should return success */
        TRASH_STACK_UNWIND (unlink, frame, 0, op_errno, preoldparent,
                            postoldparent, xdata);
out:

        if (tmp_str)
                GF_FREE (tmp_str);
        if (tmp_cookie)
                GF_FREE (tmp_cookie);
        if (new_xdata)
                dict_unref (new_xdata);

        return ret;
}

/**
 * move backs from trash translator to truncate call
 */
int32_t
trash_common_unwind_buf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *prebuf, struct iatt *postbuf,
                             dict_t *xdata)
{
        TRASH_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf,
                            postbuf, xdata);
        return 0;
}



int32_t
trash_unlink_stat_cbk (call_frame_t *frame,  void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf,
                       dict_t *xdata)
{
        trash_private_t     *priv        = NULL;
        trash_local_t       *local       = NULL;
        loc_t               new_loc      = {0,};
        int                 ret          = 0;

        priv  = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: %s",
                        local->loc.path, strerror (op_errno));
                TRASH_STACK_UNWIND (unlink, frame, op_ret, op_errno, buf,
                            NULL, xdata);
                ret = -1;
                goto out;
        }

        /* Only last hardlink will be moved to trash directory */
        if (buf->ia_nlink > 1) {
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->loc,
                            0, xdata);
                goto out;
        }

        /* if the file is too big  just unlink it */
        if (buf->ia_size > (priv->max_trash_file_size)) {
                gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file size too big (%"PRId64") to "
                                "move into trash directory",
                                local->loc.path, buf->ia_size);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->loc,
                            0, xdata);
                goto out;
        }

        /* Copies new path for renaming */
        loc_copy (&new_loc, &local->loc);
        new_loc.path = gf_strdup (local->newpath);
        if (!new_loc.path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }


        STACK_WIND (frame, trash_unlink_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    &local->loc, &new_loc, xdata);

out:
        loc_wipe (&new_loc);

        return ret;

}

/**
 * Unlink is called internally by rm system call and also
 * by internal operations of gluster such as self-heal
 */
int32_t
trash_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
              dict_t *xdata)
{
        trash_private_t         *priv           = NULL;
        trash_local_t           *local          = NULL;/* files inside trash */
        int32_t                 match           = 0;
        int32_t                 ctr_link_req    = 0;
        char                    *pathbuf        = NULL;
        int                     ret             = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        /* If trash is not active or not enabled through cli, then
         *  we bypass and wind back
         */
        if (!priv->state) {
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc, 0,
                            xdata);
                goto out;
        }

        /* The files removed by gluster internal operations such as self-heal,
         * should moved to trash directory , but files by client should not
         * moved
         */
        if ((frame->root->pid < 0) && !priv->internal) {
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc, 0,
                            xdata);
                goto out;
        }
        /* loc need some gfid which will be present in inode */
        gf_uuid_copy (loc->gfid, loc->inode->gfid);

        /* Checking for valid location */
        if (gf_uuid_is_null (loc->gfid) && gf_uuid_is_null (loc->inode->gfid)) {
                gf_log (this->name, GF_LOG_DEBUG, "Bad address");
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc, 0,
                            xdata);
                ret = EFAULT;
                goto out;
        }

        /* This will be more accurate */
        inode_path (loc->inode, NULL, &pathbuf);
        /* Check whether the file is present under eliminate paths or
         * inside trash directory. In both cases we don't need to move the
         * file to trash directory. Instead delete it permanently
         */
        match = check_whether_eliminate_path (priv->eliminate, pathbuf);
        if ((strncmp (pathbuf, priv->newtrash_dir,
                      strlen (priv->newtrash_dir)) == 0) || (match)) {
                if (match) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s is a file comes under an eliminate path, "
                                "so it is not moved to trash", loc->name);
                }

                /* Trying to unlink from the trash-dir. So do the
                 * actual unlink without moving to trash-dir.
                 */
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc, 0,
                            xdata);
                goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (unlink, frame, -1, ENOMEM, NULL, NULL,
                                    xdata);
                ret = ENOMEM;
                goto out;
        }
        frame->local = local;
        loc_copy (&local->loc, loc);

        /* rename new location of file as starting from trash directory */
        copy_trash_path (priv->newtrash_dir, (frame->root->pid < 0),
                                                        local->newpath);
        strcat (local->newpath, pathbuf);

        /* append timestamp to file name so that we can avoid
         * name collisions inside trash
         */
        append_time_stamp (local->newpath);
        if (strlen (local->newpath) > PATH_MAX) {
                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc, 0,
                            xdata);
                goto out;
        }

        /* To know whether CTR xlator requested for the link count */
        ret = dict_get_int32 (xdata, GF_REQUEST_LINK_COUNT_XDATA,
                              &ctr_link_req);
        if (ret) {
                local->ctr_link_count_req = _gf_false;
                ret = 0;
        } else
                local->ctr_link_count_req = _gf_true;

        LOCK_INIT (&frame->lock);

        STACK_WIND (frame, trash_unlink_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);
out:
        return ret;
}

/**
 * Use this when a failure occurs, and delete the newly created file
 */
int32_t
trash_truncate_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t *xdata)
{
        trash_local_t     *local  = NULL;

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "deleting the newly created file: %s",
                        strerror (op_errno));
        }

        STACK_WIND (frame, trash_common_unwind_buf_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    &local->loc, local->fop_offset, xdata);
out:
        return 0;
}

/**
 * Read from source file
 */
int32_t
trash_truncate_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct iovec *vector, int32_t count,
                          struct iatt *stbuf, struct iobref *iobuf,
                          dict_t *xdata)
{

        trash_local_t    *local  = NULL;

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "readv on the existing file failed: %s",
                        strerror (op_errno));

                STACK_WIND (frame, trash_truncate_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &local->newloc, 0, xdata);
                goto out;
        }

        local->fsize = stbuf->ia_size;
        STACK_WIND (frame, trash_truncate_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    local->newfd, vector, count, local->cur_offset, 0, iobuf,
                    xdata);

out:
        return 0;

}

/**
 * Write to file created in trash directory
 */
int32_t
trash_truncate_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *prebuf, struct iatt *postbuf,
                           dict_t *xdata)
{
        trash_local_t    *local  = NULL;

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        if (op_ret == -1) {
                /* Let truncate work, but previous copy is not preserved. */
                gf_log (this->name, GF_LOG_DEBUG,
                        "writev on the existing file failed: %s",
                        strerror (op_errno));

                STACK_WIND (frame, trash_truncate_unlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->newloc, 0,
                            xdata);
                goto out;
        }

        if (local->cur_offset < local->fsize) {
                local->cur_offset += GF_BLOCK_READV_SIZE;
                /* Loop back and Read the contents again. */
                STACK_WIND (frame, trash_truncate_readv_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                            local->fd, (size_t)GF_BLOCK_READV_SIZE,
                            local->cur_offset, 0, xdata);
                goto out;
        }


        /* OOFH.....Finally calling Truncate. */
        STACK_WIND (frame, trash_common_unwind_buf_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, &local->loc,
                    local->fop_offset, xdata);

out:
        return 0;
}

/**
 * The source file is opened for reading and writing
 */
int32_t
trash_truncate_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, fd_t *fd,
                         dict_t *xdata)
{
        trash_local_t    *local  = NULL;

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        if (op_ret == -1) {
                /* Let truncate work, but previous copy is not preserved. */
                gf_log (this->name, GF_LOG_DEBUG,
                        "open on the existing file failed: %s",
                        strerror (op_errno));

                STACK_WIND (frame, trash_truncate_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &local->newloc, 0, xdata);
                goto out;
        }

        fd_bind (fd);

        local->cur_offset = 0;

        STACK_WIND (frame, trash_truncate_readv_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                    local->fd, (size_t)GF_BLOCK_READV_SIZE, local->cur_offset,
                    0, xdata);

out:
        return 0;
}

/**
 * Creates new file descriptor for read and write operations,
 * if the path is present in trash directory
 */
int32_t
trash_truncate_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, fd_t *fd,
                           inode_t *inode, struct iatt *buf,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t *xdata)
{
        trash_local_t        *local                  = NULL;
        char                 *tmp_str                = NULL;
        char                 *dir_name               = NULL;
        char                 *tmp_path               = NULL;
        int32_t              flags                   = 0;
        loc_t                tmp_loc                 = {0,};
        char                 *tmp_stat               = NULL;
        char                 real_path[PATH_MAX]     = {0,};
        trash_private_t     *priv               = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        /* Checks whether path is present in trash directory or not */

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                /* Creating the directory structure here. */
                tmp_str = gf_strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        goto out;
                }
                dir_name = dirname (tmp_str);

                tmp_path = gf_strdup (dir_name);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        goto out;
                }
                loc_copy (&tmp_loc, &local->newloc);
                tmp_loc.path = gf_strdup (tmp_path);
                if (!tmp_loc.path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        goto out;
                }
                strcpy (real_path, priv->brick_path);
                remove_trash_path (tmp_path, (frame->root->pid < 0), &tmp_stat);
                if (tmp_stat)
                        strcat (real_path, tmp_stat);

                TRASH_SET_PID (frame, local);

                /* create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk,
                                   tmp_path, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, get_permission(real_path),
                                   0022, xdata);
                loc_wipe (&tmp_loc);
                goto out;
        }

        if (op_ret == -1) {
                /* Let truncate work, but previous copy is not preserved.
                 * Deleting the newly created copy.
                 */
                gf_log (this->name, GF_LOG_DEBUG,
                        "creation of new file in trash-dir failed, "
                        "when truncate was called: %s", strerror (op_errno));

                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, &local->loc,
                            local->fop_offset, xdata);
                goto out;
        }

        fd_bind (fd);
        flags = O_RDONLY;

        /* fd which represents source file for reading and writing from it */

        local->fd = fd_create (local->loc.inode, frame->root->pid);

        STACK_WIND (frame, trash_truncate_open_cbk,  FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, &local->loc, flags,
                    local->fd, 0);
out:
        if (tmp_str)
                GF_FREE (tmp_str);
        if (tmp_path)
                GF_FREE (tmp_path);

        return 0;
}

/**
 * If the path is not present in the trash directory,it will recursively call
 * this call-back and one by one directories will be created from the
 * beginning
 */
int32_t
trash_truncate_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *stbuf, struct iatt *preparent,
                          struct iatt *postparent, dict_t *xdata)
{
        trash_local_t        *local              = NULL;
        trash_private_t      *priv               = NULL;
        char                 *tmp_str            = NULL;
        char                 *tmp_path           = NULL;
        char                 *tmp_dirname        = NULL;
        char                 *dir_name           = NULL;
        char                 *tmp_stat           = NULL;
        char                 real_path[PATH_MAX] = {0,};
        size_t               count               = 0;
        int32_t              flags               = 0;
        int32_t              loop_count          = 0;
        int                  i                   = 0;
        loc_t                tmp_loc             = {0,};
        int                  ret                 = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        loop_count = local->loop_count;

        TRASH_UNSET_PID (frame, local);

        tmp_str = gf_strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;
                        i++;
                        if (i > loop_count)
                                break;
                        tmp_dirname = strchr (tmp_str + count + 1, '/');
                }
                tmp_path = gf_memdup (local->newpath, count + 1);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                tmp_path[count] = '\0';

                loc_copy (&tmp_loc, &local->newloc);
                tmp_loc.path = gf_strdup (tmp_path);
                if (!tmp_loc.path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }

                /* Stores the the name of directory to be created */
                tmp_loc.name = gf_strdup (strrchr(tmp_path, '/') + 1);
                if (!tmp_loc.name) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                strcpy (real_path, priv->brick_path);
                remove_trash_path (tmp_path, (frame->root->pid < 0), &tmp_stat);
                if (tmp_stat)
                        strcat (real_path, tmp_stat);

                TRASH_SET_PID (frame, local);

                STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk,
                                   tmp_path, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, get_permission(real_path),
                                   0022, xdata);
                loc_wipe (&tmp_loc);
                goto out;
        }

        if (op_ret == 0) {
                dir_name = dirname (tmp_str);
                if (strcmp ((char*)cookie, dir_name) == 0) {
                        flags = O_CREAT|O_EXCL|O_WRONLY;
                        strcpy (real_path, priv->brick_path);
                        strcat (real_path, local->origpath);
                        /* Call create again once directory structure
                           is created. */
                        STACK_WIND (frame, trash_truncate_create_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->create,
                                    &local->newloc, flags,
                                    get_permission (real_path),
                                    0022, local->newfd, xdata);
                        goto out;
                }
        }

        if ((op_ret == -1) && (op_errno != EEXIST)) {
                gf_log (this->name, GF_LOG_ERROR, "Directory creation failed [%s]. "
                                "Therefore truncating %s without moving the "
                                "original copy to trash directory",
                                strerror(op_errno), local->loc.name);
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, &local->loc,
                            local->fop_offset, xdata);
                goto out;
        }

        LOCK (&frame->lock);
        {
                loop_count = ++local->loop_count;
        }
        UNLOCK (&frame->lock);

        tmp_dirname = strchr (tmp_str, '/');
        while (tmp_dirname) {
                count = tmp_dirname - tmp_str;
                if (count == 0)
                        count = 1;
                i++;
                if (i > loop_count)
                        break;
                tmp_dirname = strchr (tmp_str + count + 1, '/');
        }
        tmp_path = gf_memdup (local->newpath, count + 1);
        if (!tmp_path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }
        tmp_path[count] = '\0';

        loc_copy (&tmp_loc, &local->newloc);
        tmp_loc.path = gf_strdup (tmp_path);
        if (!tmp_loc.path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }

        /* Stores the the name of directory to be created */
        tmp_loc.name = gf_strdup (strrchr(tmp_path, '/') + 1);
        if (!tmp_loc.name) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                goto out;
        }

        strcpy (real_path, priv->brick_path);
        remove_trash_path (tmp_path, (frame->root->pid < 0), &tmp_stat);
        if (tmp_stat)
                strcat (real_path, tmp_stat);

        TRASH_SET_PID (frame, local);

        STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk, tmp_path,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->mkdir, &tmp_loc,
                           get_permission(real_path),
                           0022, xdata);

out:
        if (tmp_str)
                GF_FREE (tmp_str);
        if (tmp_path)
                GF_FREE (tmp_path);

        return ret;
}


int32_t
trash_truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         dict_t *xdata)
{
        trash_private_t       *priv                   = NULL;
        trash_local_t         *local                  = NULL;
        char                  loc_newname[PATH_MAX]   = {0,};
        int32_t               flags                   = 0;
        dentry_t              *dir_entry              = NULL;
        inode_table_t         *table                  = NULL;
        int                   ret                     = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO ("trash", local, out);

        table = local->loc.inode->table;

        pthread_mutex_lock (&table->lock);
        {
                dir_entry = __dentry_search_arbit (local->loc.inode);
        }
        pthread_mutex_unlock (&table->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "fstat on the file failed: %s",
                        strerror (op_errno));

                TRASH_STACK_UNWIND (truncate, frame, op_ret, op_errno, buf,
                                NULL, xdata);
                goto out;
        }

        /* Only last hardlink will be moved to trash directory */
        if (buf->ia_nlink > 1) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            &local->loc, local->fop_offset, xdata);
                goto out;
        }

        /**
         * If the file is too big or if it is extended truncate,
         * just don't move it to trash directory.
         */
        if (buf->ia_size > (priv->max_trash_file_size) ||
                                buf->ia_size <= local->fop_offset) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: not moving to trash , "
                           "having inappropiate file size", local->loc.path);

                STACK_WIND (frame,  trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            &local->loc, local->fop_offset, xdata);
                goto out;
        }

        /* Retrives the name of file from path */
        local->loc.name = gf_strdup (strrchr (local->loc.path, '/'));
        if (!local->loc.name) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                goto out;
        }

        /* Stores new path for source file */
        copy_trash_path (priv->newtrash_dir, (frame->root->pid < 0),
                                                        local->newpath);
        strcat (local->newpath, local->loc.path);

        /* append timestamp to file name so that we can avoid
           name collisions inside trash */
        append_time_stamp (local->newpath);
        if (strlen (local->newpath) > PATH_MAX) {
                STACK_WIND (frame,  trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            &local->loc, local->fop_offset, xdata);
                goto out;
        }

        strcpy (loc_newname, local->loc.name);
        append_time_stamp (loc_newname);
        /* local->newloc represents old file(file inside trash),
           where as local->loc represents truncated file. We need
           to create new inode and fd for new file*/
        local->newloc.name = gf_strdup (loc_newname);
        if (!local->newloc.name) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }
        local->newloc.path = gf_strdup (local->newpath);
        if (!local->newloc.path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                ret = ENOMEM;
                goto out;
        }
        local->newloc.inode = inode_new (local->loc.inode->table);
        local->newfd = fd_create (local->newloc.inode, frame->root->pid);

        /* Creating valid parent and pargfids for both files */

        if (dir_entry == NULL) {
                ret = EINVAL;
                goto out;
        }
        local->loc.parent = inode_ref (dir_entry->parent);
        gf_uuid_copy (local->loc.pargfid, dir_entry->parent->gfid);

        local->newloc.parent = inode_ref (dir_entry->parent);
        gf_uuid_copy (local->newloc.pargfid, dir_entry->parent->gfid);

        flags = O_CREAT|O_EXCL|O_WRONLY;

        STACK_WIND (frame, trash_truncate_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    &local->newloc, flags,
                    st_mode_from_ia (buf->ia_prot, local->loc.inode->ia_type),
                    0022, local->newfd, xdata);

out:
        return ret;
}

/**
 * Truncate can be explicitly called or implicitly by some other applications
 * like text editors etc..
 */
int32_t
trash_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                off_t offset, dict_t *xdata)
{
        trash_private_t        *priv           = NULL;
        trash_local_t          *local          = NULL;
        int32_t                match           = 0;
        char                   *pathbuf        = NULL;
        int                    ret             = 0;

        priv  = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);
        /* If trash is not active or not enabled through cli, then
         * we bypass and wind back
         */
        if (!priv->state) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc,
                            offset, xdata);
                goto out;
        }

        /* The files removed by gluster operations such as self-heal,
           should moved to trash directory, but files by client should
           not moved */
        if ((frame->root->pid < 0) && !priv->internal) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc,
                            offset, xdata);
                goto out;
        }
        /* This will be more accurate */
        inode_path(loc->inode, NULL, &pathbuf);

        /* Checks whether file is in trash directory or eliminate path.
         * In all such cases it does not move to trash directory,
         * truncate will be performed
         */
        match = check_whether_eliminate_path (priv->eliminate, pathbuf);

        if ((strncmp (pathbuf, priv->newtrash_dir,
                      strlen (priv->newtrash_dir)) == 0) || (match)) {
                if (match) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file not moved to trash as per option "
                                "'eliminate path'", loc->path);
                }

                /* Trying to truncate from the trash-dir. So do the
                 * actual truncate without moving to trash-dir.
                 */
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc, offset,
                            xdata);
                goto out;
        }

        LOCK_INIT (&frame->lock);

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (truncate, frame, -1, ENOMEM, NULL, NULL,
                                    xdata);
                ret = ENOMEM;
                goto out;
        }

        strcpy (local->origpath, pathbuf);

        loc_copy (&local->loc, loc);
        local->loc.path = pathbuf;
        local->fop_offset = offset;

        frame->local = local;

        STACK_WIND (frame, trash_truncate_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc,
                    xdata);

out:
        return ret;
}

/**
 * When we call truncate from terminal it comes to ftruncate of trash-xlator.
 * Since truncate internally calls ftruncate and we receive fd of the file,
 * other than that it also called by Rebalance operation
 */
int32_t
trash_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 dict_t *xdata)
{
        trash_private_t      *priv           = NULL;
        trash_local_t        *local          = NULL;/* file inside trash */
        char                 *pathbuf        = NULL;/* path of file from fd */
        int32_t              retval          = 0;
        int32_t              match           = 0;
        int                  ret             = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);
        /* If trash is not active or not enabled through cli, then
         * we bypass and wind back
         */
        if (!priv->state) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate, fd,
                            offset, xdata);
                goto out;
        }

        /* The files removed by gluster operations such as self-heal,
         * should moved to trash directory, but files by client
         * should not moved
         */
        if ((frame->root->pid < 0) && !priv->internal) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate, fd,
                            offset, xdata);
                goto out;
        }
        /* This will be more accurate */
        retval = inode_path (fd->inode, NULL, &pathbuf);

        /* Checking  the eliminate path */

        /* Checks whether file is trash directory or eliminate path or
         * invalid fd. In all such cases it does not move to trash directory,
         * ftruncate will be performed
         */
        match = check_whether_eliminate_path (priv->eliminate, pathbuf);
        if ((strncmp (pathbuf, priv->newtrash_dir,
                      strlen (priv->newtrash_dir)) == 0) || match ||
                      !retval) {

                if (match) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file matches eliminate path, "
                                "not moved to trash", pathbuf);
                }

                /* Trying to ftruncate from the trash-dir. So do the
                 * actual ftruncate without moving to trash-dir
                 */
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            fd, offset, xdata);
                goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (ftruncate, frame, -1, ENOMEM, NULL,
                                    NULL, xdata);
                ret     = -1;
                goto out;
        }

        strcpy (local->origpath, pathbuf);

        /* To convert fd to location */
        frame->local=local;

        local->loc.path  = pathbuf;
        local->loc.inode = inode_ref (fd->inode);
        gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        local->fop_offset = offset;

        /* Else remains same to truncate code, so from here flow goes
         * to truncate_stat
         */
        STACK_WIND (frame, trash_truncate_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, xdata);
out:
        return ret;
}

/**
 * The mkdir call is intercepted to avoid creation of
 * trash directory in the mount by the user
 */
int32_t
trash_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        int32_t               op_ret      = 0;
        int32_t               op_errno    = 0;
        trash_private_t       *priv       = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        if (!check_whether_op_permitted (priv, loc)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "mkdir issued on %s, which is not permitted",
                        priv->newtrash_dir);
                op_errno = EPERM;
                op_ret = -1;

                STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno,
                             NULL, NULL, NULL, NULL, xdata);
        } else {
                STACK_WIND (frame, trash_common_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
        }

out:
        return 0;
}

/**
 * The rename call is intercepted to avoid renaming
 * of trash directory in the mount by the user
 */
int
trash_rename (call_frame_t *frame, xlator_t *this,
              loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t               op_ret      = 0;
        int32_t               op_errno    = 0;
        trash_private_t       *priv       = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        if (!check_whether_op_permitted (priv, oldloc)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "rename issued on %s, which is not permitted",
                        priv->newtrash_dir);
                op_errno = EPERM;
                op_ret = -1;

                STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, NULL,
                                     NULL, NULL, NULL, NULL, xdata);
        } else {
                STACK_WIND (frame, trash_common_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
        }

out:
       return 0;
}

/**
 * The rmdir call is intercepted to avoid deletion of
 * trash directory in the mount by the user
 */
int32_t
trash_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int flags, dict_t *xdata)
{
        int32_t               op_ret      = 0;
        int32_t               op_errno    = 0;
        trash_private_t       *priv       = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        if (!check_whether_op_permitted (priv, loc)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "rmdir issued on %s, which is not permitted",
                        priv->newtrash_dir);
                op_errno = EPERM;
                op_ret = -1;

                STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             NULL, NULL, xdata);
        } else {
                STACK_WIND (frame, trash_common_rmdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc, flags, xdata);
        }

out:
       return 0;
}

/**
 * Volume set option is handled by the reconfigure funtion.
 * Here we checks whether each option is set or not ,if it
 * sets then corresponding modifciations will be made
 */
int
reconfigure (xlator_t *this, dict_t *options)
{
        uint64_t              max_fsize                         = 0;
        int                   ret                               = 0;
        char                  *tmp                              = NULL;
        char                  *tmp_str                          = NULL;
        trash_private_t       *priv                             = NULL;
        char                  trash_dir[PATH_MAX]               = {0,};

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("trash", priv, out);

        GF_OPTION_RECONF ("trash-internal-op", priv->internal, options,
                                               bool, out);
        GF_OPTION_RECONF ("trash-dir", tmp, options, str, out);

        GF_OPTION_RECONF ("trash", priv->state, options, bool, out);

        if (priv->state) {
                ret = create_or_rename_trash_directory (this);

                if (tmp)
                        sprintf(trash_dir, "/%s/", tmp);
                else
                        sprintf(trash_dir, "%s", priv->oldtrash_dir);

                if (strcmp(priv->newtrash_dir, trash_dir) != 0) {

                        /* When user set a new name for trash directory, trash
                        * xlator will perform a rename operation on old trash
                        * directory to the new one using a STACK_WIND from here.
                        * This option can be configured only when volume is in
                        * started state
                        */

                        GF_FREE (priv->newtrash_dir);

                        priv->newtrash_dir = gf_strdup (trash_dir);
                        if (!priv->newtrash_dir) {
                                ret = ENOMEM;
                                gf_log (this->name, GF_LOG_DEBUG,
                                                "out of memory");
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Renaming %s -> %s from reconfigure",
                                priv->oldtrash_dir, priv->newtrash_dir);

                        if (!priv->newtrash_dir) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                                "out of memory");
                                ret = ENOMEM;
                                goto out;
                        }
                        ret = rename_trash_directory (this);

                }

                if (priv->internal) {
                        ret = create_internalop_directory (this);

                }
        }
        tmp = NULL;

        GF_OPTION_RECONF ("trash-max-filesize", max_fsize, options,
                                                size_uint64, out);
        if (max_fsize) {
                priv->max_trash_file_size = max_fsize;
                gf_log (this->name, GF_LOG_DEBUG, "%"GF_PRI_SIZET" max-size",
                        priv->max_trash_file_size);
        }
        GF_OPTION_RECONF ("trash-eliminate-path", tmp, options, str, out);
        if (!tmp) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no option specified for 'eliminate', using NULL");
        } else {
                if (priv->eliminate)
                        wipe_eliminate_path (&priv->eliminate);

                tmp_str = gf_strdup (tmp);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                ret = store_eliminate_path (tmp_str, &priv->eliminate);

        }

out:

        return ret;
}

/**
 * Notify is used to create the trash directory with fixed gfid
 * using STACK_WIND only when posix xlator is up
 */
int
notify (xlator_t *this, int event, void *data, ...)
{
        trash_private_t       *priv      = NULL;
        int                   ret        = 0;

        priv = this->private;
        GF_VALIDATE_OR_GOTO ("trash", priv, out);

       /* Check whether posix is up not */
        if (event == GF_EVENT_CHILD_UP) {

                if (!priv->state) {
                        gf_log (this->name, GF_LOG_DEBUG, "trash xlator is off");
                        goto out;
                }

                /* Here there is two possiblities ,if trash directory already
                 * exist ,then we need to perform a rename operation on the
                 * old one. Otherwise, we need to create the trash directory
                 * For both, we need to pass location variable, gfid of parent
                 * and a frame for calling STACK_WIND.The location variable
                 * requires name,path,gfid and inode
                 */
                if (!priv->oldtrash_dir)
                        ret = create_or_rename_trash_directory (this);
                else if (strcmp(priv->newtrash_dir, priv->oldtrash_dir) != 0)
                        ret = rename_trash_directory (this);
                if (ret)
                        goto out;

                if (priv->internal)
                        ret = create_internalop_directory (this);

        }
out:
        ret = default_notify (this, event, data);
        if (ret)
                gf_log (this->name, GF_LOG_INFO,
                        "default notify event failed");
        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int         ret          = -1;

        GF_VALIDATE_OR_GOTO ("trash", this, out);

        ret = xlator_mem_acct_init (this, gf_trash_mt_end + 1);
        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
                return ret;
        }
out:
        return ret;
}

/**
 * trash_init
 */
int32_t
init (xlator_t *this)
{
        trash_private_t        *priv                   = NULL;
        int                    ret                     = -1;
        char                   *tmp                    = NULL;
        char                   *tmp_str                = NULL;
        char                   trash_dir[PATH_MAX]     = {0,};
        uint64_t               max_trash_file_size64   = 0;
        data_t                *data                    = NULL;

        GF_VALIDATE_OR_GOTO ("trash", this, out);

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "not configured with exactly one child. exiting");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_trash_mt_trash_private_t);
        if (!priv) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = ENOMEM;
                goto out;
        }

        /* Trash priv data members are initialized through the following
         * set of statements
         */
        GF_OPTION_INIT ("trash", priv->state, bool, out);

        GF_OPTION_INIT ("trash-dir", tmp, str, out);

        /* We store trash dir value as path for easier manipulation*/
        if (!tmp) {
                gf_log (this->name, GF_LOG_INFO,
                        "no option specified for 'trash-dir', "
                        "using \"/.trashcan/\"");
                priv->newtrash_dir = gf_strdup ("/.trashcan/");
                if (!priv->newtrash_dir) {
                        ret = ENOMEM;
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        goto out;
                }
        } else {
                sprintf(trash_dir, "/%s/", tmp);
                priv->newtrash_dir = gf_strdup (trash_dir);
                if (!priv->newtrash_dir) {
                        ret = ENOMEM;
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        goto out;
                }
        }
        tmp = NULL;

        GF_OPTION_INIT ("trash-eliminate-path", tmp, str, out);
        if (!tmp) {
                gf_log (this->name, GF_LOG_INFO,
                        "no option specified for 'eliminate', using NULL");
        } else {
                tmp_str = gf_strdup (tmp);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_ERROR,
                                        "out of memory");
                        ret = ENOMEM;
                        goto out;
                }
                ret = store_eliminate_path (tmp_str, &priv->eliminate);

        }
        tmp = NULL;

        GF_OPTION_INIT ("trash-max-filesize", max_trash_file_size64,
                        size_uint64, out);
        if (!max_trash_file_size64) {
                gf_log (this->name, GF_LOG_ERROR,
                        "no option specified for 'max-trashable-file-size', "
                        "using default = %lld MB",
                        GF_DEFAULT_MAX_FILE_SIZE / GF_UNIT_MB);
                priv->max_trash_file_size = GF_DEFAULT_MAX_FILE_SIZE;
        } else {
                priv->max_trash_file_size = max_trash_file_size64;
                gf_log (this->name, GF_LOG_DEBUG, "%"GF_PRI_SIZET" max-size",
                        priv->max_trash_file_size);
        }

        GF_OPTION_INIT ("trash-internal-op", priv->internal, bool, out);

        this->local_pool = mem_pool_new (trash_local_t, 64);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                ret = ENOMEM;
                goto out;
        }

        /* For creating directories inside trash with proper permissions,
         * we need to perform stat on that directories, for this we use
         * brick path
         */
        data = dict_get (this->options, "brick-path");
        if (!data) {
                gf_log (this->name, GF_LOG_ERROR,
                        "no option specified for 'brick-path'");
                ret = ENOMEM;
                goto out;
        }
        priv->brick_path = gf_strdup (data->data);
        if (!priv->brick_path) {
                ret = ENOMEM;
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                goto out;
        }

        priv->trash_itable = inode_table_new (0, this);
        gf_log (this->name, GF_LOG_DEBUG, "brick path is%s", priv->brick_path);

        this->private = (void *)priv;
        ret = 0;

out:
        if (tmp_str)
                GF_FREE (tmp_str);
        if (ret) {
                if (priv) {
                        if (priv->newtrash_dir)
                                GF_FREE (priv->newtrash_dir);
                        if (priv->oldtrash_dir)
                                GF_FREE (priv->oldtrash_dir);
                        if (priv->brick_path)
                                GF_FREE (priv->brick_path);
                        if (priv->eliminate)
                                wipe_eliminate_path (&priv->eliminate);
                        GF_FREE (priv);
                }
                mem_pool_destroy (this->local_pool);
        }
        return ret;
}

/**
 * trash_fini
 */
void
fini (xlator_t *this)
{
        trash_private_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("trash", this, out);
        priv = this->private;

        if (priv) {
                if (priv->newtrash_dir)
                        GF_FREE (priv->newtrash_dir);
                if (priv->oldtrash_dir)
                        GF_FREE (priv->oldtrash_dir);
                if (priv->brick_path)
                        GF_FREE (priv->brick_path);
                if (priv->eliminate)
                        wipe_eliminate_path (&priv->eliminate);
                GF_FREE (priv);
        }
        mem_pool_destroy (this->local_pool);
        this->private = NULL;
out:
        return;
}

struct xlator_fops fops = {
        .unlink          = trash_unlink,
        .truncate        = trash_truncate,
        .ftruncate       = trash_ftruncate,
        .rmdir           = trash_rmdir,
        .mkdir           = trash_mkdir,
        .rename          = trash_rename,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key           = { "trash" },
          .type          = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description   = "Enable/disable trash translator",
        },
        { .key           = { "trash-dir" },
          .type          = GF_OPTION_TYPE_STR,
          .default_value = ".trashcan",
          .description   = "Directory for trash files",
        },
        { .key           = { "trash-eliminate-path" },
          .type          = GF_OPTION_TYPE_STR,
          .description   = "Eliminate paths to be excluded "
                           "from trashing",
        },
        { .key           = { "trash-max-filesize" },
          .type          = GF_OPTION_TYPE_SIZET,
          .default_value = "5MB",
          .description   = "Maximum size of file that can be "
                           "moved to trash",
        },
        { .key           = { "trash-internal-op" },
          .type          = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description   = "Enable/disable trash translator for "
                           "internal operations",
        },
        { .key           = {NULL} },
};
