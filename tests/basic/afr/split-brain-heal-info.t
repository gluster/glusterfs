#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function volume_start_force()
{
        local vol=$1
        TEST $CLI volume start $vol force
        EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $vol 0
        EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $vol 1
}

TESTS_EXPECTED_IN_LOOP=15
SPB_FILES=0
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dspb
TEST mkdir $M0/mspb
TEST mkdir $M0/espb
TEST touch $M0/dspb/file

#### Simlulate data-split-brain
TEST kill_brick  $V0 $H0 $B0/${V0}0
TEST `echo "abc" > $M0/dspb/file`
volume_start_force $V0
TEST kill_brick  $V0 $H0 $B0/${V0}1
TEST `echo "def" > $M0/dspb/file`
volume_start_force $V0
SPB_FILES=$(($SPB_FILES + 1))

### Simulate metadata-split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST chmod 757 $M0/mspb
volume_start_force $V0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST chmod 747 $M0/mspb
volume_start_force $V0
SPB_FILES=$(($SPB_FILES + 1))

#### Simulate entry-split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/espb/a
volume_start_force $V0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST mkdir $M0/espb/a
volume_start_force $V0
SPB_FILES=$(($SPB_FILES + 1))

#Multiply by 2, for each brick in replica pair
SPB_FILES=$(($SPB_FILES * 2))
EXPECT "$SPB_FILES" afr_get_split_brain_count $V0
cleanup;
