/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "bit-rot-ssm.h"
#include "bit-rot-scrub.h"
#include "bit-rot-bitd-messages.h"

int br_scrub_ssm_noop (xlator_t *this, br_child_t *child)
{
        return 0;
}

int
br_scrub_ssm_state_pause (xlator_t *this, br_child_t *child)
{
        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                "Scrubber paused [Brick: %s]", child->brick_path);
        _br_child_set_scrub_state (child, BR_SCRUB_STATE_PAUSED);
        return 0;
}

int
br_scrub_ssm_state_ipause (xlator_t *this, br_child_t *child)
{
        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                "Scrubber paused [Brick: %s]", child->brick_path);
        _br_child_set_scrub_state (child, BR_SCRUB_STATE_IPAUSED);
        return 0;
}

int
br_scrub_ssm_state_active (xlator_t *this, br_child_t *child)
{
        struct br_scanfs *fsscan = &child->fsscan;

        if (fsscan->over) {
                (void) br_fsscan_activate (this, child);
        } else {
                gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                        "Scrubbing resumed [Brick %s]", child->brick_path);
                _br_child_set_scrub_state (child, BR_SCRUB_STATE_ACTIVE);
        }

        return 0;
}

int
br_scrub_ssm_state_stall (xlator_t *this, br_child_t *child)
{
        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                "Brick [%s] is under active scrubbing. Pausing scrub..",
                child->brick_path);
        _br_child_set_scrub_state (child, BR_SCRUB_STATE_STALLED);
        return 0;
}

static br_scrub_ssm_call *
br_scrub_ssm[BR_SCRUB_MAXSTATES][BR_SCRUB_MAXEVENTS] = {
        {br_fsscan_schedule, br_scrub_ssm_state_ipause},    /* INACTIVE */
        {br_fsscan_reschedule, br_fsscan_deactivate},       /* PENDING  */
        {br_scrub_ssm_noop, br_scrub_ssm_state_stall},      /* ACTIVE   */
        {br_fsscan_activate, br_scrub_ssm_noop},            /* PAUSED   */
        {br_fsscan_schedule, br_scrub_ssm_noop},            /* IPAUSED  */
        {br_scrub_ssm_state_active, br_scrub_ssm_noop},     /* STALLED  */
};

int32_t
br_scrub_state_machine (xlator_t *this, br_child_t *child)
{
        br_private_t       *priv      = NULL;
        br_scrub_ssm_call  *call      = NULL;
        struct br_scanfs   *fsscan    = NULL;
        struct br_scrubber *fsscrub   = NULL;
        br_scrub_state_t    currstate = 0;
        br_scrub_event_t    event     = 0;

        priv = this->private;
        fsscan = &child->fsscan;
        fsscrub = &priv->fsscrub;

        currstate = fsscan->state;
        event = _br_child_get_scrub_event (fsscrub);

        call = br_scrub_ssm[currstate][event];
        return call (this, child);
}
