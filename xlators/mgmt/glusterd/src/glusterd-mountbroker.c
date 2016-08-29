/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <inttypes.h>
#include <fnmatch.h>
#include <pwd.h>

#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "list.h"
#include "logging.h"
#include "syscall.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "run.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-utils.h"
#include "common-utils.h"
#include "glusterd-mountbroker.h"
#include "glusterd-op-sm.h"
#include "glusterd-messages.h"

static int
seq_dict_foreach (dict_t *dict,
                  int (*fn)(char *str, void *data),
                  void *data)
{
        char index[] = "4294967296"; // 1<<32
        int        i = 0;
        char    *val = NULL;
        int      ret = 0;

        for (;;i++) {
                snprintf(index, sizeof(index), "%d", i);
                ret = dict_get_str (dict, index, &val);
                if (ret != 0)
                        return ret == -ENOENT ? 0 : ret;
                ret = fn (val, data);
                if (ret != 0)
                        return ret;
        }
}

int
parse_mount_pattern_desc (gf_mount_spec_t *mspec, char *pdesc)
#define SYNTAX_ERR -2
{
        char *curs              = NULL;
        char *c2                = NULL;
        char sc                 = '\0';
        char **cc               = NULL;
        gf_mount_pattern_t *pat = NULL;
        int pnum                = 0;
        int ret                 = 0;
        int lastsup             = -1;
        int incl                = -1;
        char **pcc              = NULL;
        int pnc                 = 0;

        skipwhite (&pdesc);

        /* a bow to theory */
        if (!*pdesc)
                return 0;

        /* count number of components, separated by '&' */
        mspec->len = 0;
        for (curs = pdesc; *curs; curs++) {
                if (*curs == ')')
                        mspec->len++;
        }

        mspec->patterns = GF_CALLOC (mspec->len, sizeof (*mspec->patterns),
                                     gf_gld_mt_mount_pattern);
        if (!mspec->patterns) {
                ret = -1;
                goto out;
        }

        pat = mspec->patterns;
        curs = pdesc;
        skipwhite (&curs);
        for (;;) {
                incl = -1;

                /* check for pattern signedness modifier */
                if (*curs == '-') {
                        pat->negative = _gf_true;
                        curs++;
                }

                /* now should come condition specifier,
                 * then opening paren
                 */
                c2 = nwstrtail (curs, "SUB(");
                if (c2) {
                        pat->condition = SET_SUB;
                        goto got_cond;
                }
                c2 = nwstrtail (curs, "SUP(");
                if (c2) {
                        pat->condition = SET_SUPER;
                        lastsup = pat - mspec->patterns;
                        goto got_cond;
                }
                c2 = nwstrtail (curs, "EQL(");
                if (c2) {
                        pat->condition = SET_EQUAL;
                        goto got_cond;
                }
                c2 = nwstrtail (curs, "MEET(");
                if (c2) {
                        pat->condition = SET_INTERSECT;
                        goto got_cond;
                }
                c2 = nwstrtail (curs, "SUB+(");
                if (c2) {
                        pat->condition = SET_SUB;
                        incl = lastsup;
                        goto got_cond;
                }

                ret = SYNTAX_ERR;
                goto out;

 got_cond:
                curs = c2;
                skipwhite (&curs);
                /* count the number of components for pattern */
                pnum = *curs == ')' ? 0 : 1;
                for (c2 = curs ;*c2 != ')';) {
                        if (strchr ("&|", *c2)) {
                                ret = SYNTAX_ERR;
                                goto out;
                        }
                        while (!strchr ("|&)", *c2) && !isspace (*c2))
                                c2++;
                        skipwhite (&c2);
                        switch (*c2) {
                        case ')':
                                break;
                        case '\0':
                        case '&':
                                ret = SYNTAX_ERR;
                                goto out;
                        case '|':
                                *c2 = ' ';
                                skipwhite (&c2);
                                /* fall through */
                        default:
                                pnum++;
                        }
                }
                if (incl >= 0) {
                        pnc = 0;
                        for (pcc = mspec->patterns[incl].components; *pcc; pcc++)
                                pnc++;
                        pnum += pnc;
                }
                pat->components = GF_CALLOC (pnum + 1, sizeof (*pat->components),
                                             gf_gld_mt_mount_comp_container);
                if (!pat->components) {
                        ret = -1;
                        goto out;
                }

                cc = pat->components;
                /* copy over included component set */
                if (incl >= 0) {
                        memcpy (pat->components,
                                mspec->patterns[incl].components,
                                pnc * sizeof (*pat->components));
                        cc += pnc;
                }
                /* parse and add components */
                c2 = ""; /* reset c2 */
                while (*c2 != ')') {
                        c2 = curs;
                        while (!isspace (*c2) && *c2 != ')')
                                c2++;
                        sc = *c2;
                        *c2 = '\0';;
                        *cc = gf_strdup (curs);
                        if (!*cc) {
                                ret = -1;
                                goto out;
                        }
                        *c2 = sc;
                        skipwhite (&c2);
                        curs = c2;
                        cc++;
                }

                curs++;
                skipwhite (&curs);
                if (*curs == '&') {
                        curs++;
                        skipwhite (&curs);
                }

                if (!*curs)
                        break;
                pat++;
        }

 out:
        if (ret == SYNTAX_ERR) {
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "cannot parse mount patterns %s",
                        pdesc);
        }

        /* We've allocted a lotta stuff here but don't bother with freeing
         * on error, in that case we'll terminate anyway
         */
        return ret ? -1 : 0;
}
#undef SYNTAX_ERR


const char *georep_mnt_desc_template =
        "SUP("
                "aux-gfid-mount "
                "acl "
                "volfile-server=localhost "
                "client-pid=%d "
                "user-map-root=%s "
        ")"
        "SUB+("
                "log-file="DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"*/* "
                "log-level=* "
                "volfile-id=* "
        ")"
        "MEET("
                "%s"
        ")";

const char *hadoop_mnt_desc_template =
        "SUP("
                "volfile-server=%s "
                "client-pid=%d "
                "volfile-id=%s "
                "user-map-root=%s "
        ")"
        "SUB+("
                "log-file="DEFAULT_LOG_FILE_DIRECTORY"/"GHADOOP"*/* "
                "log-level=* "
        ")";

int
make_georep_mountspec (gf_mount_spec_t *mspec, const char *volnames,
                       char *user)
{
        char *georep_mnt_desc = NULL;
        char *meetspec        = NULL;
        char *vols            = NULL;
        char *vol             = NULL;
        char *p               = NULL;
        char *savetok         = NULL;
        char *fa[3]           = {0,};
        size_t siz            = 0;
        int vc                = 0;
        int i                 = 0;
        int ret               = 0;

        vols = gf_strdup ((char *)volnames);
        if (!vols)
                goto out;

        for (vc = 1, p = vols; *p; p++) {
                if (*p == ',')
                        vc++;
        }
        siz = strlen (volnames) + vc * strlen("volfile-id=");
        meetspec = GF_CALLOC (1, siz + 1, gf_gld_mt_georep_meet_spec);
        if (!meetspec)
                goto out;

        for (p = vols;;) {
                vol = strtok_r (p, ",", &savetok);
                if (!vol) {
                        GF_ASSERT (vc == 0);
                        break;
                }
                p = NULL;
                strcat (meetspec, "volfile-id=");
                strcat (meetspec, vol);
                if (--vc > 0)
                        strcat (meetspec, " ");
        }

        ret = gf_asprintf (&georep_mnt_desc, georep_mnt_desc_template,
                           GF_CLIENT_PID_GSYNCD, user, meetspec);
        if (ret == -1) {
                georep_mnt_desc = NULL;
                goto out;
        }

        ret = parse_mount_pattern_desc (mspec, georep_mnt_desc);

 out:
        fa[0] = meetspec;
        fa[1] = vols;
        fa[2] = georep_mnt_desc;

        for (i = 0; i < 3; i++) {
                if (fa[i] == NULL)
                        ret = -1;
                else
                        GF_FREE (fa[i]);
        }

        return ret;
}

int
make_ghadoop_mountspec (gf_mount_spec_t *mspec, const char *volname,
                        char *user, char *server)
{
        char *hadoop_mnt_desc = NULL;
        int   ret             = 0;

        ret = gf_asprintf (&hadoop_mnt_desc, hadoop_mnt_desc_template,
                           server, GF_CLIENT_PID_HADOOP, volname, user);
        if (ret == -1)
                return ret;

        return parse_mount_pattern_desc (mspec, hadoop_mnt_desc);
}

static gf_boolean_t
match_comp (char *str, char *patcomp)
{
        char *c1 = patcomp;
        char *c2 = str;

        GF_ASSERT (c1);
        GF_ASSERT (c2);

        while (*c1 == *c2) {
                if (!*c1)
                        return _gf_true;
                c1++;
                c2++;
                if (c1[-1] == '=')
                        break;
        }

        return fnmatch (c1, c2, 0) == 0 ? _gf_true : _gf_false;
}

struct gf_set_descriptor {
        gf_boolean_t priv[2];
        gf_boolean_t common;
};

static int
_gf_set_dict_iter1 (char *val, void *data)
{
        void **dataa                 = data;
        struct gf_set_descriptor *sd = dataa[0];
        char **curs                  = dataa[1];
        gf_boolean_t priv            = _gf_true;

        while (*curs) {
                if (match_comp (val, *curs)) {
                        priv = _gf_false;
                        sd->common = _gf_true;
                }
                curs++;
        }

        if (priv)
                sd->priv[0] = _gf_true;

        return 0;
}

static int
_gf_set_dict_iter2 (char *val, void *data)
{
        void **dataa      = data;
        gf_boolean_t *boo = dataa[0];
        char *comp        = dataa[1];

        if (match_comp (val, comp))
                *boo = _gf_true;

        return 0;
}

static void
relate_sets (struct gf_set_descriptor *sd, dict_t *argdict, char **complist)
{
        void *dataa[] = {NULL, NULL};
        gf_boolean_t boo = _gf_false;

        memset (sd, 0, sizeof (*sd));

        dataa[0] = sd;
        dataa[1] = complist;
        seq_dict_foreach (argdict, _gf_set_dict_iter1, dataa);

        while (*complist) {
                boo = _gf_false;
                dataa[0] = &boo;
                dataa[1] = *complist;
                seq_dict_foreach (argdict, _gf_set_dict_iter2, dataa);

                if (boo)
                        sd->common = _gf_true;
                else
                        sd->priv[1] = _gf_true;

                complist++;
        }
}

static int
_arg_parse_uid (char *val, void *data)
{
        char *user        = strtail (val, "user-map-root=");
        struct passwd *pw = NULL;

        if (!user)
                return 0;
        pw = getpwnam (user);
        if (!pw)
                return -EINVAL;

        if (*(int *)data >= 0)
                /* uid ambiguity, already found */
                return -EINVAL;

        *(int *)data = pw->pw_uid;
        return 0;
}

static int
evaluate_mount_request (xlator_t *this, gf_mount_spec_t *mspec, dict_t *argdict)
{
        struct gf_set_descriptor sd = {{0,},};
        int i                       = 0;
        int uid                     = -1;
        int ret                     = 0;
        gf_boolean_t match          = _gf_false;

        for (i = 0; i < mspec->len; i++) {
                relate_sets (&sd, argdict, mspec->patterns[i].components);
                switch (mspec->patterns[i].condition) {
                case SET_SUB:
                        match = !sd.priv[0];
                        break;
                case SET_SUPER:
                        match = !sd.priv[1];
                        break;
                case SET_EQUAL:
                        match = (!sd.priv[0] && !sd.priv[1]);
                        break;
                case SET_INTERSECT:
                        match = sd.common;
                        break;
                default:
                        GF_ASSERT(!"unreached");
                }
                if (mspec->patterns[i].negative)
                        match = !match;

                if (!match) {
                        gf_msg (this->name, GF_LOG_ERROR, EPERM,
                                GD_MSG_MNTBROKER_SPEC_MISMATCH,
                                "Mountbroker spec mismatch!!! SET: %d "
                                "COMPONENT: %d. Review the mount args passed",
                                 mspec->patterns[i].condition, i);
                        return -EPERM;
                }
        }

        ret = seq_dict_foreach (argdict, _arg_parse_uid, &uid);
        if (ret != 0)
                return ret;

        return uid;
}

static int
_volname_get (char *val, void *data)
{
        char **volname = data;

        *volname = strtail (val, "volfile-id=");

        return *volname ? 1 : 0;
}

static int
_runner_add (char *val, void *data)
{
        runner_t *runner = data;

        runner_argprintf (runner, "--%s", val);

        return 0;
}

int
glusterd_do_mount (char *label, dict_t *argdict, char **path, int *op_errno)
{
        glusterd_conf_t *priv      = NULL;
        char *mountbroker_root     = NULL;
        gf_mount_spec_t *mspec     = NULL;
        int uid                    = -ENOENT;
        char *volname              = NULL;
        glusterd_volinfo_t *vol    = NULL;
        char *mtptemp              = NULL;
        char *mntlink              = NULL;
        char *cookieswitch         = NULL;
        char *cookie               = NULL;
        char *sla                  = NULL;
        struct stat st             = {0,};
        runner_t runner            = {0,};
        int ret                    = 0;
        xlator_t *this             = THIS;
        mode_t orig_umask          = 0;
        gf_boolean_t found_label   = _gf_false;

        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (op_errno);
        *op_errno = 0;

        if (dict_get_str (this->options, "mountbroker-root",
                          &mountbroker_root) != 0) {
                *op_errno = ENOENT;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "'option mountbroker-root' "
                        "missing in glusterd vol file");
                goto out;
        }

        GF_ASSERT (label);
        if (!*label) {
                *op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_MNTBROKER_LABEL_NULL,
                        "label is NULL (%s)",
                        strerror (*op_errno));
                goto out;
        }

        /* look up spec for label */
        cds_list_for_each_entry (mspec, &priv->mount_specs,
                                 speclist) {
                if (strcmp (mspec->label, label) != 0)
                        continue;

                found_label = _gf_true;
                uid = evaluate_mount_request (this, mspec, argdict);
                break;
        }
        if (uid < 0) {
                *op_errno = -uid;
                if (!found_label) {
                        gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                                GD_MSG_MNTBROKER_LABEL_MISS,
                                "Missing mspec: Check the corresponding option "
                                "in glusterd vol file for mountbroker user: %s",
                                 label);
                }
                goto out;
        }

        /* some sanity check on arguments */
        seq_dict_foreach (argdict, _volname_get, &volname);
        if (!volname) {
                *op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_DICT_GET_FAILED,
                        "Dict get failed for the key 'volname'");
                goto out;
        }
        if (glusterd_volinfo_find (volname, &vol) != 0 ||
            !glusterd_is_volume_started (vol)) {
                *op_errno = ENOENT;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_MOUNT_REQ_FAIL,
                        "Either volume is not started or volinfo not found");
                goto out;
        }

        /* go do mount */

        /** create actual mount dir */

        /*** "overload" string name to be possible to used for cookie
             creation, see below */
        ret = gf_asprintf (&mtptemp, "%s/user%d/mtpt-%s-XXXXXX/cookie",
                           mountbroker_root, uid, label);
        if (ret == -1) {
                mtptemp = NULL;
                *op_errno = ENOMEM;
                goto out;
        }
        /*** hide cookie part */
        cookieswitch = strrchr (mtptemp, '/');
        *cookieswitch = '\0';

        sla = strrchr (mtptemp, '/');
        *sla = '\0';
        ret = sys_mkdir (mtptemp, 0700);
        if (ret == 0)
                ret = sys_chown (mtptemp, uid, 0);
        else if (errno == EEXIST)
                ret = 0;
        if (ret == -1) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_SYSCALL_FAIL,
                        "Mountbroker User directory creation failed");
                goto out;
        }
        ret = sys_lstat (mtptemp, &st);
        if (ret == -1) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_SYSCALL_FAIL,
                        "stat on mountbroker user directory failed");
                goto out;
        }
        if (!(S_ISDIR (st.st_mode) && (st.st_mode & ~S_IFMT) == 0700 &&
              st.st_uid == uid && st.st_gid == 0)) {
                *op_errno = EACCES;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_MOUNT_REQ_FAIL,
                        "Incorrect mountbroker user directory attributes");
                goto out;
        }
        *sla = '/';

        if (!mkdtemp (mtptemp)) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_SYSCALL_FAIL,
                        "Mountbroker mount directory creation failed");
                goto out;
        }

        /** create private "cookie" symlink */

        /*** occupy an entry in the hive dir via mkstemp */
        ret = gf_asprintf (&cookie, "%s/"MB_HIVE"/mntXXXXXX",
                           mountbroker_root);
        if (ret == -1) {
                cookie = NULL;
                *op_errno = ENOMEM;
                goto out;
        }
        orig_umask = umask(S_IRWXG | S_IRWXO);
        ret = mkstemp (cookie);
        umask(orig_umask);
        if (ret == -1) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_SYSCALL_FAIL,
                        "Mountbroker cookie file creation failed");
                goto out;
        }
        sys_close (ret);

        /*** assembly the path from cookie to mountpoint */
        sla = strchr (sla - 1, '/');
        GF_ASSERT (sla);
        ret = gf_asprintf (&mntlink, "../user%d%s", uid, sla);
        if (ret == -1) {
                *op_errno = ENOMEM;
                goto out;
        }

        /*** create cookie link in (to-be) mountpoint,
             move it over to the final place */
        *cookieswitch = '/';
        ret = sys_symlink (mntlink, mtptemp);
        if (ret != -1)
                ret = sys_rename (mtptemp, cookie);
        *cookieswitch = '\0';
        if (ret == -1) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, *op_errno,
                        GD_MSG_SYSCALL_FAIL,
                        "symlink or rename failed");
                goto out;
        }

        /** invoke glusterfs on the mountpoint */

        runinit (&runner);
        runner_add_arg (&runner, SBIN_DIR"/glusterfs");
        seq_dict_foreach (argdict, _runner_add, &runner);
        runner_add_arg (&runner, mtptemp);
        ret = runner_run_reuse (&runner);
        if (ret == -1) {
                *op_errno = EIO; /* XXX hacky fake */
                runner_log (&runner, "", GF_LOG_ERROR, "command failed");
        }
        runner_end (&runner);

 out:

        if (*op_errno) {
                ret = -1;
                gf_msg (this->name, GF_LOG_WARNING, *op_errno,
                        GD_MSG_MOUNT_REQ_FAIL,
                        "unsuccessful mount request");
                if (mtptemp) {
                        *cookieswitch = '/';
                        sys_unlink (mtptemp);
                        *cookieswitch = '\0';
                        sys_rmdir (mtptemp);
                }
                if (cookie) {
                        sys_unlink (cookie);
                        GF_FREE (cookie);
                }

        } else {
                ret = 0;
                *path = cookie;
        }

        GF_FREE (mtptemp);

        return ret;
}
