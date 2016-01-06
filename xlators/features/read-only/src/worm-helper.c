/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "read-only-mem-types.h"
#include "read-only.h"
#include "xlator.h"
#include "syncop.h"
#include "worm-helper.h"

/*Function to check whether file is read-only.
 * The input *stbuf contains the attributes of the file, which is used to check
 * the write protection bits for all the users of the file.
 * Return true if all the write bits are disabled,false otherwise*/
gf_boolean_t
gf_worm_write_disabled (struct iatt *stbuf)
{
        gf_boolean_t ret        =       _gf_false;

        GF_VALIDATE_OR_GOTO ("worm", stbuf, out);

        if (stbuf->ia_prot.owner.write == 0 &&
            stbuf->ia_prot.group.write == 0 &&
            stbuf->ia_prot.other.write == 0)
                ret = _gf_true;
out:
        return ret;
}


int32_t
worm_init_state (xlator_t *this, gf_boolean_t fop_with_fd, void *file_ptr)
{
        int ret                 =      -1;
        uint64_t start_time     =       0;
        dict_t *dict            =       NULL;

        GF_VALIDATE_OR_GOTO ("worm", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file_ptr, out);

        start_time = time (NULL);
        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "Error creating the dict");
                goto out;
        }
        ret = dict_set_uint64 (dict, "trusted.start_time", start_time);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Error in setting the dict");
                goto out;
        }
        if (fop_with_fd)
                ret = syncop_fsetxattr (this, (fd_t *)file_ptr, dict, 0,
                                        NULL, NULL);
        else
                ret = syncop_setxattr (this, (loc_t *)file_ptr, dict, 0, NULL,
                                       NULL);
out:
        if (dict)
                dict_unref (dict);
        return ret;
}


/*Function to set the retention state for a file.
 * It loads the WORM/Retention state into the retention_state pointer.*/
int32_t
worm_set_state (xlator_t *this, gf_boolean_t fop_with_fd, void *file_ptr,
                worm_reten_state_t *retention_state, struct iatt *stbuf)
{
        read_only_priv_t *priv   =      NULL;
        struct iatt stpre        =      {0,};
        int ret                  =      -1;

        GF_VALIDATE_OR_GOTO ("worm", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file_ptr, out);
        GF_VALIDATE_OR_GOTO (this->name, retention_state, out);
        GF_VALIDATE_OR_GOTO (this->name, stbuf, out);

        priv = this->private;
        GF_ASSERT (priv);
        retention_state->worm = 1;
        retention_state->retain = 1;
        retention_state->legal_hold = 0;
        if (strcmp (priv->reten_mode, "relax") == 0)
                retention_state->ret_mode = 0;
        else
                retention_state->ret_mode = 1;
        retention_state->ret_period = priv->reten_period;
        retention_state->auto_commit_period = priv->com_period;
        if (fop_with_fd)
                ret = syncop_fstat (this, (fd_t *)file_ptr, &stpre, NULL, NULL);
        else
                ret = syncop_stat (this, (loc_t *)file_ptr, &stpre, NULL, NULL);
        if (ret)
                goto out;
        stbuf->ia_mtime = stpre.ia_mtime;
        stbuf->ia_atime = time (NULL) + retention_state->ret_period;

        if (fop_with_fd)
                ret = syncop_fsetattr (this, (fd_t *)file_ptr, stbuf,
                                       GF_SET_ATTR_ATIME, NULL, NULL,
                                       NULL, NULL);
        else
                ret = syncop_setattr (this, (loc_t *)file_ptr, stbuf,
                                      GF_SET_ATTR_ATIME, NULL, NULL,
                                      NULL, NULL);
        if (ret)
                goto out;

        ret = gf_worm_set_xattr (this, retention_state, fop_with_fd, file_ptr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Error setting xattr");
                goto out;
        }
        ret = 0;
out:
        return ret;
}


/*This function gets the state of the WORM/Retention xattr and loads it in the
 * dict pointer.*/
int32_t
worm_get_state (xlator_t *this, gf_boolean_t fop_with_fd, void *file_ptr,
                worm_reten_state_t *reten_state)
{
        dict_t *dict    =       NULL;
        char *val       =       NULL;
        int ret         =       -1;

        GF_VALIDATE_OR_GOTO ("worm", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file_ptr, out);
        GF_VALIDATE_OR_GOTO (this->name, reten_state, out);

        if (fop_with_fd)
                ret = syncop_fgetxattr (this, (fd_t *)file_ptr, &dict,
                                        "trusted.reten_state", NULL, NULL);
        else
                ret = syncop_getxattr (this, (loc_t *)file_ptr, &dict,
                                       "trusted.reten_state", NULL, NULL);
        if (ret < 0 || !dict) {
                ret = -1;
                goto out;
        }
        ret = dict_get_str (dict, "trusted.reten_state", &val);
        if (ret) {
                ret = -2;
                gf_log (this->name, GF_LOG_ERROR, "Empty val");
        }
        gf_worm_deserialize_state (val, reten_state);
out:
        if (dict)
                dict_unref (dict);
        return ret;
}


/*Function to lookup the current state of the WORM/Retention profile.
 * Based on the retain value and the access time of the file, the transition
 * from WORM/Retention to WORM is made.*/
void
gf_worm_state_lookup (xlator_t *this, gf_boolean_t fop_with_fd, void *file_ptr,
                      worm_reten_state_t *reten_state, struct iatt *stbuf)
{
        int ret                                 =       -1;

        GF_VALIDATE_OR_GOTO ("worm", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file_ptr, out);
        GF_VALIDATE_OR_GOTO (this->name, reten_state, out);
        GF_VALIDATE_OR_GOTO (this->name, stbuf, out);

        stbuf->ia_atime -= reten_state->ret_period;
        reten_state->retain = 0;
        reten_state->ret_period = 0;
        reten_state->auto_commit_period = 0;
        ret = gf_worm_set_xattr (this, reten_state, fop_with_fd, file_ptr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Error setting xattr");
                goto out;
        }

        if (fop_with_fd)
                ret = syncop_fsetattr (this, (fd_t *)file_ptr, stbuf,
                                       GF_SET_ATTR_ATIME, NULL, NULL,
                                       NULL, NULL);
        else
                ret = syncop_setattr (this, (loc_t *)file_ptr, stbuf,
                                      GF_SET_ATTR_ATIME, NULL, NULL,
                                      NULL, NULL);
        if (ret)
                goto out;
        gf_log (this->name, GF_LOG_INFO, "Retention state reset");
out:
        return;
}


/*This function serializes and stores the WORM/Retention state of a file in an
 * uint64_t variable by setting the bits using the bitwise operations.*/
void
gf_worm_serialize_state (worm_reten_state_t *reten_state, char *val)
{
        uint32_t state     =       0;

        GF_VALIDATE_OR_GOTO ("worm", reten_state, out);
        GF_VALIDATE_OR_GOTO ("worm", val, out);

        state |= reten_state->worm << 0;
        state |= reten_state->retain << 1;
        state |= reten_state->legal_hold << 2;
        state |= reten_state->ret_mode << 3;
        sprintf (val, "%d/%"PRIu64"/%"PRIu64, state, reten_state->ret_period,
                 reten_state->auto_commit_period);

out:
        return;
}


/*This function deserializes the data stored in the xattr of the file and loads
 * the value to the reten_state structure.*/
void
gf_worm_deserialize_state (char *val, worm_reten_state_t *reten_state)
{
        char *token     =       NULL;
        uint32_t state  =       0;

        GF_VALIDATE_OR_GOTO ("worm", val, out);
        GF_VALIDATE_OR_GOTO ("worm", reten_state, out);

        token = strtok (val, "/");
        state = atoi (token);
        reten_state->worm = (state >> 0) & 1;
        reten_state->retain = (state >> 1) & 1;
        reten_state->legal_hold = (state >> 2) & 1;
        reten_state->ret_mode = (state >> 3) & 1;
        token = strtok (NULL, "/");
        reten_state->ret_period = atoi (token);
        token = strtok (NULL, "/");
        reten_state->auto_commit_period = atoi (token);

out:
        return;
}


/*Function to set the xattr for a file.
 * If the xattr is already present then it will replace that.*/
int32_t
gf_worm_set_xattr (xlator_t *this, worm_reten_state_t *reten_state,
                   gf_boolean_t fop_with_fd, void *file_ptr)
{
        char val[100]   =        "";
        int ret         =        -1;
        dict_t *dict    =        NULL;

        GF_VALIDATE_OR_GOTO ("worm", this, out);
        GF_VALIDATE_OR_GOTO (this->name, reten_state, out);
        GF_VALIDATE_OR_GOTO (this->name, file_ptr, out);

        gf_worm_serialize_state (reten_state, val);
        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "Error creating the dict");
                goto out;
        }
        ret = dict_set_str (dict, "trusted.reten_state", val);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Error in setting the dict");
                goto out;
        }
        if (fop_with_fd)
                ret = syncop_fsetxattr (this, (fd_t *)file_ptr, dict, 0,
                                        NULL, NULL);
        else
                ret = syncop_setxattr (this, (loc_t *)file_ptr, dict, 0, NULL,
                                       NULL);
out:
        if (dict)
                dict_unref (dict);
        return ret;
}


/*This function checks whether a file's timeout is happend for the state
 * transition and if yes, then it will do the transition from the current state
 * to the appropriate state. It also decides whether to continue or to block
 * the FOP.
 * Return:
 * 0 : If the FOP should continue i.e., if the file is not in the WORM-Retained
 *     state or if the FOP is unlink and the file is not in the Retained state.
 * 1: If the FOP sholud block i.e., if the file is in WORM-Retained/WORM state.
 * 2: Blocks the FOP if any operation fails while doing the state transition or
 *    fails to get the state of the file.*/
int
gf_worm_state_transition (xlator_t *this, gf_boolean_t fop_with_fd,
                          void *file_ptr, glusterfs_fop_t op)
{
        int op_errno                      =       EROFS;
        int ret                           =       -1;
        uint64_t com_period               =       0;
        uint64_t start_time               =       0;
        dict_t *dict                      =       NULL;
        worm_reten_state_t reten_state    =       {0,};
        read_only_priv_t *priv            =       NULL;
        struct iatt stbuf                 =       {0,};

        priv = this->private;
        GF_ASSERT (priv);

        if (fop_with_fd)
                ret = syncop_fgetxattr (this, (fd_t *)file_ptr, &dict,
                                        "trusted.start_time", NULL, NULL);
        else
                ret = syncop_getxattr (this, (loc_t *)file_ptr, &dict,
                                       "trusted.start_time", NULL, NULL);
        if (ret < 0 || !dict) {
                op_errno = ret;
                gf_msg (this->name, GF_LOG_ERROR, -ret, 0,
                        "Error getting xattr");
                goto out;
        }
        ret = dict_get_uint64 (dict, "trusted.start_time", &start_time);
        if (ret) {
                op_errno = ret;
                gf_msg (this->name, GF_LOG_ERROR, -ret, 0,
                        "Error getting start time");
                goto out;
        }

        com_period = priv->com_period;
        if (fop_with_fd)
                ret = syncop_fstat (this, (fd_t *)file_ptr, &stbuf, NULL, NULL);
        else
                ret = syncop_stat (this, (loc_t *)file_ptr, &stbuf, NULL, NULL);
        if (ret) {
                op_errno = ret;
                gf_msg (this->name, GF_LOG_ERROR, -ret, 0,
                        "Error getting file stat");
                goto out;
        }

        ret = worm_get_state (this, fop_with_fd, file_ptr, &reten_state);
        if (ret == -2) {
                op_errno = ret;
                gf_msg (this->name, GF_LOG_ERROR, -ret, 0,
                        "Error getting worm/retention state");
                goto out;
        }

        if (ret == -1 && (time (NULL) - start_time) >= com_period) {
                if ((time (NULL) - stbuf.ia_mtime) >= com_period) {
                        ret = worm_set_state(this, fop_with_fd, file_ptr,
                                             &reten_state, &stbuf);
                        if (ret) {
                                op_errno = ret;
                                gf_msg (this->name, GF_LOG_ERROR, -ret, 0,
                                        "Error setting worm/retention state");
                                goto out;
                        }
                        goto out;
                } else {
                        op_errno = 0;
                        goto out;
                }
        } else if (ret == -1 && (time (NULL) - start_time)
                   < com_period) {
                op_errno = 0;
                goto out;
        } else if (reten_state.retain &&
                   ((time (NULL) >= stbuf.ia_atime))) {
                gf_worm_state_lookup (this, fop_with_fd, file_ptr,
                                      &reten_state, &stbuf);
        }
        if (reten_state.worm && !reten_state.retain &&
                 op == GF_FOP_UNLINK) {
                op_errno = 0;
                goto out;
        }

out:
        if (dict)
                dict_unref (dict);
        return op_errno;
}


/*Function to check whether a file is independently WORMed (i.e., file level
 * WORM is set on the file). */
int32_t
is_wormfile (xlator_t *this, gf_boolean_t fop_with_fd, void *file_ptr)
{
        int ret         =       -1;
        dict_t *dict    =       NULL;

        if (fop_with_fd)
                ret = syncop_fgetxattr (this, (fd_t *)file_ptr, &dict,
                                       "trusted.worm_file", NULL, NULL);
        else
                ret = syncop_getxattr (this, (loc_t *)file_ptr, &dict,
                                       "trusted.worm_file", NULL, NULL);
        if (dict) {
                ret = 0;
                dict_unref (dict);
        }
        return ret;
}
