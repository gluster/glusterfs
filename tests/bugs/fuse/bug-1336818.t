#!/bin/bash

#Test case: OOM score adjust

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Prepare
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

# Basic check
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST umount $M0

# Check valid value (< 0)
TEST glusterfs --oom-score-adj=-1000 -s $H0 --volfile-id $V0 $M0
TEST umount $M0

# Check valid value (> 0)
TEST glusterfs --oom-score-adj=1000 -s $H0 --volfile-id $V0 $M0
TEST umount $M0

# Check valid value (= 0)
TEST glusterfs --oom-score-adj=0 -s $H0 --volfile-id $V0 $M0
TEST umount $M0

# Check invalid value (no value given)
TEST ! glusterfs --oom-score-adj -s $H0 --volfile-id $V0 $M0

# Check invalid value (< OOM_SCORE_ADJ_MIN)
TEST ! glusterfs --oom-score-adj=-1001 -s $H0 --volfile-id $V0 $M0

# Check invalid value (> OOM_SCORE_ADJ_MAX)
TEST ! glusterfs --oom-score-adj=1001 -s $H0 --volfile-id $V0 $M0

# Check invalid value (float)
TEST ! glusterfs --oom-score-adj=12.34 -s $H0 --volfile-id $V0 $M0

# Check invalid value (non-integer string)
TEST ! glusterfs --oom-score-adj=qwerty -s $H0 --volfile-id $V0 $M0

cleanup;
