#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test contains volume heal commands handled by glusterd.
# Covers enable/disable at the moment. Will be enhanced later to include
# the other commands as well.

function is_pid_running {
    local pid=$1
    num=`ps auxww | grep glustershd | grep $pid | grep -v grep | wc -l`
    echo $num
}

cleanup;
TEST glusterd
TEST pidof glusterd

#Commands should fail when volume doesn't exist
TEST ! $CLI volume heal non-existent-volume enable
TEST ! $CLI volume heal non-existent-volume disable

# Glustershd shouldn't be running as long as there are no replicate/disperse
# volumes
TEST $CLI volume create dist $H0:$B0/dist
TEST $CLI volume start dist
TEST "[ -z $(get_shd_process_pid dist)]"
TEST ! $CLI volume heal dist enable
TEST ! $CLI volume heal dist disable

# Commands should work on replicate/disperse volume.
TEST $CLI volume create r2 replica 2 $H0:$B0/r2_0 $H0:$B0/r2_1
TEST "[ -z $(get_shd_process_pid r2)]"
TEST $CLI volume start r2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid r2
TEST $CLI volume heal r2 enable
EXPECT "enable" volume_option r2 "cluster.self-heal-daemon"
volfiler2=$(gluster system:: getwd)"/vols/r2/r2-shd.vol"
EXPECT "enable" volgen_volume_option $volfiler2 r2-replicate-0 cluster replicate self-heal-daemon
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid r2
pid=$( get_shd_process_pid r2 )
TEST $CLI volume heal r2 disable
EXPECT "disable" volume_option r2 "cluster.self-heal-daemon"
EXPECT "disable" volgen_volume_option $volfiler2 r2-replicate-0 cluster replicate self-heal-daemon
EXPECT "1" is_pid_running $pid

# Commands should work on disperse volume.
TEST $CLI volume create ec2 disperse 3 redundancy 1 $H0:$B0/ec2_0 $H0:$B0/ec2_1 $H0:$B0/ec2_2
TEST $CLI volume start ec2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid ec2
TEST $CLI volume heal ec2 enable
EXPECT "enable" volume_option ec2 "cluster.disperse-self-heal-daemon"
volfileec2=$(gluster system:: getwd)"/vols/ec2/ec2-shd.vol"
EXPECT "enable" volgen_volume_option $volfileec2 ec2-disperse-0 cluster disperse self-heal-daemon
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid ec2
pid=$(get_shd_process_pid ec2)
TEST $CLI volume heal ec2 disable
EXPECT "disable" volume_option ec2 "cluster.disperse-self-heal-daemon"
EXPECT "disable" volgen_volume_option $volfileec2 ec2-disperse-0 cluster disperse self-heal-daemon
EXPECT "1" is_pid_running $pid

#Check that shd graph is rewritten correctly on volume stop/start
EXPECT "Y" volgen_volume_exists $volfileec2 ec2-disperse-0 cluster disperse

EXPECT "Y" volgen_volume_exists $volfiler2 r2-replicate-0 cluster replicate
TEST $CLI volume stop r2
EXPECT "Y" volgen_volume_exists $volfileec2 ec2-disperse-0 cluster disperse
TEST $CLI volume stop ec2
# When both the volumes are stopped glustershd volfile is not modified just the
# process is stopped
TEST "[ -z $(get_shd_process_pid dist) ]"
TEST "[ -z $(get_shd_process_pid ec2) ]"

TEST $CLI volume start r2
EXPECT "Y" volgen_volume_exists $volfiler2 r2-replicate-0 cluster replicate

TEST $CLI volume set r2 self-heal-daemon on
TEST $CLI volume set r2 cluster.self-heal-daemon off
TEST ! $CLI volume set ec2 self-heal-daemon off
TEST ! $CLI volume set ec2 cluster.self-heal-daemon on
TEST ! $CLI volume set dist self-heal-daemon off
TEST ! $CLI volume set dist cluster.self-heal-daemon on

TEST $CLI volume set ec2 disperse-self-heal-daemon off
TEST $CLI volume set ec2 cluster.disperse-self-heal-daemon on
TEST ! $CLI volume set r2 disperse-self-heal-daemon on
TEST ! $CLI volume set r2 cluster.disperse-self-heal-daemon off
TEST ! $CLI volume set dist disperse-self-heal-daemon off
TEST ! $CLI volume set dist cluster.disperse-self-heal-daemon on

cleanup
