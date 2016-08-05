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

int br_scrub_ssm_noop (xlator_t *this)
{
        return 0;
}

int
br_scrub_ssm_state_pause (xlator_t *this)
{
        br_private_t        *priv               = NULL;
        struct br_monitor   *scrub_monitor      = NULL;

        priv = this->private;
        scrub_monitor = &priv->scrub_monitor;

        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                "Scrubber paused");
        _br_monitor_set_scrub_state (scrub_monitor, BR_SCRUB_STATE_PAUSED);
        return 0;
}

int
br_scrub_ssm_state_ipause (xlator_t *this)
{
        br_private_t        *priv               = NULL;
        struct br_monitor   *scrub_monitor      = NULL;

        priv = this->private;
        scrub_monitor = &priv->scrub_monitor;

        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                "Scrubber paused");
        _br_monitor_set_scrub_state (scrub_monitor, BR_SCRUB_STATE_IPAUSED);
        return 0;
}

int
br_scrub_ssm_state_active (xlator_t *this)
{
        br_private_t        *priv               = NULL;
        struct br_monitor   *scrub_monitor      = NULL;

        priv = this->private;
        scrub_monitor = &priv->scrub_monitor;

        if (scrub_monitor->done) {
                (void) br_fsscan_activate (this);
        } else {
                gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                        "Scrubbing resumed");
                _br_monitor_set_scrub_state (scrub_monitor, BR_SCRUB_STATE_ACTIVE);
        }

        return 0;
}

int
br_scrub_ssm_state_stall (xlator_t *this)
{
        br_private_t        *priv               = NULL;
        struct br_monitor   *scrub_monitor      = NULL;

        priv = this->private;
        scrub_monitor = &priv->scrub_monitor;

        gf_msg (this->name, GF_LOG_INFO, 0, BRB_MSG_GENERIC_SSM_INFO,
                "Volume is under active scrubbing. Pausing scrub..");
        _br_monitor_set_scrub_state (scrub_monitor, BR_SCRUB_STATE_STALLED);
        return 0;
}

static br_scrub_ssm_call *
br_scrub_ssm[BR_SCRUB_MAXSTATES][BR_SCRUB_MAXEVENTS] = {
        /* INACTIVE */
        {br_fsscan_schedule, br_scrub_ssm_state_ipause, br_scrub_ssm_noop},
        /* PENDING  */
        {br_fsscan_reschedule, br_fsscan_deactivate, br_fsscan_ondemand},
        /* ACTIVE   */
        {br_scrub_ssm_noop, br_scrub_ssm_state_stall, br_scrub_ssm_noop},
        /* PAUSED   */
        {br_fsscan_activate, br_scrub_ssm_noop, br_scrub_ssm_noop},
        /* IPAUSED  */
        {br_fsscan_schedule, br_scrub_ssm_noop, br_scrub_ssm_noop},
        /* STALLED  */
        {br_scrub_ssm_state_active, br_scrub_ssm_noop, br_scrub_ssm_noop},
};

int32_t
br_scrub_state_machine (xlator_t *this, gf_boolean_t scrub_ondemand)
{
        br_private_t       *priv      = NULL;
        br_scrub_ssm_call  *call      = NULL;
        struct br_scrubber *fsscrub   = NULL;
        br_scrub_state_t    currstate = 0;
        br_scrub_event_t    event     = 0;
        struct br_monitor  *scrub_monitor = NULL;

        priv = this->private;
        fsscrub = &priv->fsscrub;
        scrub_monitor = &priv->scrub_monitor;

        currstate = scrub_monitor->state;
        if (scrub_ondemand)
                event = BR_SCRUB_EVENT_ONDEMAND;
        else
                event = _br_child_get_scrub_event (fsscrub);

        call = br_scrub_ssm[currstate][event];
        return call (this);
}
