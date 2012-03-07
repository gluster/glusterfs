/*
   Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "fuse-bridge.h"
#if defined(GF_SOLARIS_HOST_OS)
#include <sys/procfs.h>
#else
#include <sys/sysctl.h>
#endif


static void
fuse_resolve_wipe (fuse_resolve_t *resolve)
{
        if (resolve->path)
                GF_FREE ((void *)resolve->path);

        if (resolve->bname)
                GF_FREE ((void *)resolve->bname);

        if (resolve->resolved)
                GF_FREE ((void *)resolve->resolved);

        if (resolve->fd)
                fd_unref (resolve->fd);

        loc_wipe (&resolve->resolve_loc);

	if (resolve->hint) {
		inode_unref (resolve->hint);
		resolve->hint = 0;
	}

	if (resolve->parhint) {
		inode_unref (resolve->parhint);
		resolve->parhint = 0;
	}
}


void
free_fuse_state (fuse_state_t *state)
{
        xlator_t       *this     = NULL;
        fuse_private_t *priv     = NULL;
        uint64_t        winds    = 0;
        char            switched = 0;

        this = state->this;

        priv = this->private;

        loc_wipe (&state->loc);

        loc_wipe (&state->loc2);

        if (state->dict) {
                dict_unref (state->dict);
                state->dict = (void *)0xaaaaeeee;
        }
        if (state->name) {
                GF_FREE (state->name);
                state->name = NULL;
        }
        if (state->fd) {
                fd_unref (state->fd);
                state->fd = (void *)0xfdfdfdfd;
        }
        if (state->finh) {
                GF_FREE (state->finh);
                state->finh = NULL;
        }

        fuse_resolve_wipe (&state->resolve);
        fuse_resolve_wipe (&state->resolve2);

        pthread_mutex_lock (&priv->sync_mutex);
        {
                winds = --state->active_subvol->winds;
                switched = state->active_subvol->switched;
        }
        pthread_mutex_unlock (&priv->sync_mutex);

        if ((winds == 0) && (switched)) {
                xlator_notify (state->active_subvol, GF_EVENT_PARENT_DOWN,
                               state->active_subvol, NULL);
        }

#ifdef DEBUG
        memset (state, 0x90, sizeof (*state));
#endif
        GF_FREE (state);
        state = NULL;
}


fuse_state_t *
get_fuse_state (xlator_t *this, fuse_in_header_t *finh)
{
        fuse_state_t   *state         = NULL;
	xlator_t       *active_subvol = NULL;
        fuse_private_t *priv          = NULL;

        state = (void *)GF_CALLOC (1, sizeof (*state),
                                   gf_fuse_mt_fuse_state_t);
        if (!state)
                return NULL;

	state->this = THIS;
        priv = this->private;

        pthread_mutex_lock (&priv->sync_mutex);
        {
                active_subvol = fuse_active_subvol (state->this);
                active_subvol->winds++;
        }
        pthread_mutex_unlock (&priv->sync_mutex);

	state->active_subvol = active_subvol;
	state->itable = active_subvol->itable;

        state->pool = this->ctx->pool;
        state->finh = finh;
        state->this = this;

        LOCK_INIT (&state->lock);

        return state;
}


void
frame_fill_groups (call_frame_t *frame)
{
#if defined(GF_LINUX_HOST_OS)
        char         filename[32];
        char         line[4096];
        char        *ptr = NULL;
        FILE        *fp = NULL;
        int          idx = 0;
        long int     id = 0;
        char        *saveptr = NULL;
        char        *endptr = NULL;
        int          ret = 0;

        ret = snprintf (filename, sizeof filename, "/proc/%d/status", frame->root->pid);
        if (ret >= sizeof filename)
                goto out;

        fp = fopen (filename, "r");
        if (!fp)
                goto out;

        while ((ptr = fgets (line, sizeof line, fp))) {
                if (strncmp (ptr, "Groups:", 7) != 0)
                        continue;

                ptr = line + 8;

                for (ptr = strtok_r (ptr, " \t\r\n", &saveptr);
                     ptr;
                     ptr = strtok_r (NULL, " \t\r\n", &saveptr)) {
                        errno = 0;
                        id = strtol (ptr, &endptr, 0);
                        if (errno == ERANGE)
                                break;
                        if (!endptr || *endptr)
                                break;
                        frame->root->groups[idx++] = id;
                        if (idx == GF_MAX_AUX_GROUPS)
                                break;
                }

                frame->root->ngrps = idx;
                break;
        }
out:
        if (fp)
                fclose (fp);
#elif defined(GF_SOLARIS_HOST_OS)
        char         filename[32];
        char         scratch[128];
        prcred_t    *prcred = (prcred_t *) scratch;
        FILE        *fp = NULL;
        int          ret = 0;

        ret = snprintf (filename, sizeof filename,
                        "/proc/%d/cred", frame->root->pid);

        if (ret < sizeof filename) {
                fp = fopen (filename, "r");
                if (fp != NULL) {
                        if (fgets (scratch, sizeof scratch, fp) != NULL) {
                                frame->root->ngrps = MIN(prcred->pr_ngroups,
                                                         GF_REQUEST_MAXGROUPS);
                        }
                        fclose (fp);
                 }
         }
#elif defined(CTL_KERN) /* DARWIN and *BSD */
        /* 
           N.B. CTL_KERN is an enum on Linux. (Meaning, if it's not
           obvious, that it's not subject to preprocessor directives 
           like '#if defined'.)
           Unlike Linux, on Mac OS and the BSDs it is a #define. We
           could test to see that KERN_PROC is defined, but, barring any 
           evidence to the contrary, I think that's overkill.
           We might also test that GF_DARWIN_HOST_OS is defined, why
           limit this to just Mac OS. It's equally valid for the BSDs
           and we do have people building on NetBSD and FreeBSD.
        */
        int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, frame->root->pid };
        size_t namelen = sizeof name / sizeof name[0];
        struct kinfo_proc kp;
        size_t kplen = sizeof(kp);
        int i, ngroups;

        if (sysctl(name, namelen, &kp, &kplen, NULL, 0) != 0)
                return;
        ngroups = MIN(kp.kp_eproc.e_ucred.cr_ngroups, GF_REQUEST_MAXGROUPS);
        for (i = 0; i < ngroups; i++)
                frame->root->groups[i] = kp.kp_eproc.e_ucred.cr_groups[i];
        frame->root->ngrps = ngroups;
#else
        frame->root->ngrps = 0;
#endif /* GF_LINUX_HOST_OS */
}

call_frame_t *
get_call_frame_for_req (fuse_state_t *state)
{
        call_pool_t           *pool = NULL;
        fuse_in_header_t      *finh = NULL;
        call_frame_t          *frame = NULL;
        xlator_t              *this = NULL;
        fuse_private_t        *priv = NULL;

        pool = state->pool;
        finh = state->finh;
        this = state->this;
        priv = this->private;

        frame = create_frame (this, pool);
        if (!frame)
                return NULL;

        if (finh) {
                frame->root->uid      = finh->uid;
                frame->root->gid      = finh->gid;
                frame->root->pid      = finh->pid;
                frame->root->unique   = finh->unique;
                set_lk_owner_from_uint64 (&frame->root->lk_owner,
                                          state->lk_owner);
        }

        frame_fill_groups (frame);

        if (priv && priv->client_pid_set)
                frame->root->pid = priv->client_pid;

        frame->root->type = GF_OP_TYPE_FOP;

        return frame;
}


inode_t *
fuse_ino_to_inode (uint64_t ino, xlator_t *fuse)
{
        inode_t  *inode = NULL;
        xlator_t *active_subvol = NULL;

        if (ino == 1) {
                active_subvol = fuse_active_subvol (fuse);
                if (active_subvol)
                        inode = active_subvol->itable->root;
        } else {
                inode = (inode_t *) (unsigned long) ino;
                inode_ref (inode);
        }

        return inode;
}

uint64_t
inode_to_fuse_nodeid (inode_t *inode)
{
        if (!inode || __is_root_gfid (inode->gfid))
                return 1;

        return (unsigned long) inode;
}


GF_MUST_CHECK int32_t
fuse_loc_fill (loc_t *loc, fuse_state_t *state, ino_t ino,
               ino_t par, const char *name)
{
        inode_t  *inode = NULL;
        inode_t  *parent = NULL;
        int32_t   ret = -1;
        char     *path = NULL;
        uuid_t    null_gfid = {0,};

        /* resistance against multiple invocation of loc_fill not to get
           reference leaks via inode_search() */

        if (name) {
                parent = loc->parent;
                if (!parent) {
                        parent = fuse_ino_to_inode (par, state->this);
                        loc->parent = parent;
                        if (parent)
                                uuid_copy (loc->pargfid, parent->gfid);
                }

                inode = loc->inode;
                if (!inode) {
                        inode = inode_grep (parent->table, parent, name);
                        loc->inode = inode;
                }

                ret = inode_path (parent, name, &path);
                if (ret <= 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "inode_path failed for %s/%s",
                                (parent)?uuid_utoa (parent->gfid):"0", name);
                        goto fail;
                }
                loc->path = path;
        } else {
                inode = loc->inode;
                if (!inode) {
                        inode = fuse_ino_to_inode (ino, state->this);
                        loc->inode = inode;
                        if (inode)
                                uuid_copy (loc->gfid, inode->gfid);
                }

                parent = loc->parent;
                if (!parent) {
                        parent = inode_parent (inode, null_gfid, NULL);
                        loc->parent = parent;
                        if (parent)
                                uuid_copy (loc->pargfid, parent->gfid);

                }

                ret = inode_path (inode, NULL, &path);
                if (ret <= 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "inode_path failed for %s",
                                (inode) ? uuid_utoa (inode->gfid) : "0");
                        goto fail;
                }
                loc->path = path;
        }

        if (loc->path) {
                loc->name = strrchr (loc->path, '/');
                if (loc->name)
                        loc->name++;
                else
                        loc->name = "";
        }

        if ((ino != 1) && (parent == NULL)) {
                gf_log ("fuse-bridge", GF_LOG_DEBUG,
                        "failed to search parent for %"PRId64"/%s (%"PRId64")",
                        (ino_t)par, name, (ino_t)ino);
                ret = -1;
                goto fail;
        }
        ret = 0;
fail:
        return ret;
}


/* courtesy of folly */
void
gf_fuse_stat2attr (struct iatt *st, struct fuse_attr *fa)
{
        fa->ino        = st->ia_ino;
        fa->size       = st->ia_size;
        fa->blocks     = st->ia_blocks;
        fa->atime      = st->ia_atime;
        fa->mtime      = st->ia_mtime;
        fa->ctime      = st->ia_ctime;
        fa->atimensec  = st->ia_atime_nsec;
        fa->mtimensec  = st->ia_mtime_nsec;
        fa->ctimensec  = st->ia_ctime_nsec;
        fa->mode       = st_mode_from_ia (st->ia_prot, st->ia_type);
        fa->nlink      = st->ia_nlink;
        fa->uid        = st->ia_uid;
        fa->gid        = st->ia_gid;
        fa->rdev       = makedev (ia_major (st->ia_rdev),
                                  ia_minor (st->ia_rdev));
#if FUSE_KERNEL_MINOR_VERSION >= 9
        fa->blksize    = st->ia_blksize;
#endif
#ifdef GF_DARWIN_HOST_OS
        fa->crtime     = (uint64_t)-1;
        fa->crtimensec = (uint32_t)-1;
        fa->flags      = 0;
#endif
}

static int
fuse_do_flip_xattr_ns (char *okey, const char *nns, char **nkey)
{
        int   ret = 0;
        char *key = NULL;

        okey = strchr (okey, '.');
        GF_ASSERT (okey);

        key = GF_CALLOC (1, strlen (nns) + strlen(okey) + 1,
                         gf_common_mt_char);
        if (!key) {
                ret = -1;
                goto out;
        }

        strcpy (key, nns);
        strcat (key, okey);

        *nkey = key;

 out:
        return ret;
}

int
fuse_xattr_alloc_default (char *okey, char **nkey)
{
        int ret = 0;

        *nkey = gf_strdup (okey);
        if (!*nkey)
                ret = -1;
        return ret;
}

#define PRIV_XA_NS   "trusted"
#define UNPRIV_XA_NS "system"

int
fuse_flip_xattr_ns (fuse_private_t *priv, char *okey, char **nkey)
{
        int             ret       = 0;
        gf_boolean_t    need_flip = _gf_false;
        gf_client_pid_t npid      = 0;

        npid = priv->client_pid;
        if (gf_client_pid_check (npid)) {
                ret = fuse_xattr_alloc_default (okey, nkey);
                goto out;
        }

        switch (npid) {
                /*
                 * These two cases will never execute as we check the
                 * pid range above, but are kept to keep the compiler
                 * happy.
                 */
        case GF_CLIENT_PID_MAX:
        case GF_CLIENT_PID_MIN:
                goto out;

        case GF_CLIENT_PID_GSYNCD:
                /* valid xattr(s): *xtime, volume-mark* */
                gf_log("glusterfs-fuse", GF_LOG_DEBUG, "PID: %d, checking xattr(s): "
                       "volume-mark*, *xtime", npid);
                if ( (strcmp (okey, UNPRIV_XA_NS".glusterfs.volume-mark") == 0)
                     || (fnmatch (UNPRIV_XA_NS".glusterfs.volume-mark.*", okey, FNM_PERIOD) == 0)
                     || (fnmatch (UNPRIV_XA_NS".glusterfs.*.xtime", okey, FNM_PERIOD) == 0) )
                        need_flip = _gf_true;
                break;

        case GF_CLIENT_PID_HADOOP:
                /* valid xattr(s): pathinfo */
                gf_log("glusterfs-fuse", GF_LOG_DEBUG, "PID: %d, checking xattr(s): "
                       "pathinfo", npid);
                if (strcmp (okey, UNPRIV_XA_NS".glusterfs.pathinfo") == 0)
                        need_flip = _gf_true;
                break;
        }

        if (need_flip) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG, "flipping %s to "PRIV_XA_NS" equivalent",
                        okey);
                ret = fuse_do_flip_xattr_ns (okey, PRIV_XA_NS, nkey);
        } else {
                /* if we cannot match, continue with what we got */
                ret = fuse_xattr_alloc_default (okey, nkey);
        }
 out:
        return ret;
}
