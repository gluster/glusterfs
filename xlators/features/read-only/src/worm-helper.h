/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

gf_boolean_t gf_worm_write_disabled (struct iatt *stbuf);

int32_t worm_init_state (xlator_t *this, gf_boolean_t fop_with_fd,
                         void *file_ptr);

int32_t worm_set_state (xlator_t *this, gf_boolean_t fop_with_fd,
                        void *file_ptr, worm_reten_state_t *retention_state,
                        struct iatt *stbuf);

int32_t worm_get_state (xlator_t *this, gf_boolean_t fop_with_fd,
                        void *file_ptr, worm_reten_state_t *reten_state);

void gf_worm_state_lookup (xlator_t *this, gf_boolean_t fop_with_fd,
                           void *file_ptr, worm_reten_state_t *reten_state,
                           struct iatt *stbuf);

void gf_worm_serialize_state (worm_reten_state_t *reten_state, char *val);

void gf_worm_deserialize_state (char *val, worm_reten_state_t *reten_state);

int32_t gf_worm_set_xattr (xlator_t *this, worm_reten_state_t *reten_state,
                           gf_boolean_t fop_with_fd, void *file_ptr);

int gf_worm_state_transition (xlator_t *this, gf_boolean_t fop_with_fd,
                              void *file_ptr, glusterfs_fop_t op);

int32_t is_wormfile (xlator_t *this, gf_boolean_t fop_with_fd, void *file_ptr);
