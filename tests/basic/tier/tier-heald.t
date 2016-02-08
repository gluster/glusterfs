#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test contains volume heal commands handled by glusterd.
# Covers enable/disable at the moment. Will be enhanced later to include
# the other commands as well.

cleanup;
TEST glusterd
TEST pidof glusterd

volfile=$(gluster system:: getwd)"/glustershd/glustershd-server.vol"

# Commands should fail when both tiers are not of distribute type.
# Glustershd shouldn't be running as long as there are no replicate/disperse
# volumes
TEST $CLI volume create dist_tier $H0:$B0/cold
TEST $CLI volume start dist_tier
TEST $CLI volume attach-tier dist_tier $H0:$B0/hot

TEST "[ -z $(get_shd_process_pid)]"
TEST ! $CLI volume heal dist_tier enable
TEST ! $CLI volume heal dist_tier disable

# Commands should work on replicate/disperse volume.
TEST $CLI volume create r2 replica 2 $H0:$B0/r2_0 $H0:$B0/r2_1
TEST "[ -z $(get_shd_process_pid)]"
TEST $CLI volume start r2

TEST $CLI volume attach-tier r2 $H0:$B0/r2_hot

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid
TEST $CLI volume heal r2 enable
EXPECT "enable" volume_option r2 "cluster.self-heal-daemon"
EXPECT "enable" volgen_volume_option $volfile r2-replicate-0 cluster replicate self-heal-daemon
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid
TEST $CLI volume heal r2 disable
EXPECT "disable" volume_option r2 "cluster.self-heal-daemon"
EXPECT "disable" volgen_volume_option $volfile r2-replicate-0 cluster replicate self-heal-daemon
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid
# Commands should work on disperse volume.
TEST $CLI volume create ec2 disperse 3 redundancy 1 $H0:$B0/ec2_0 $H0:$B0/ec2_1 $H0:$B0/ec2_2
TEST $CLI volume start ec2

TEST $CLI volume attach-tier ec2 replica 2 $H0:$B0/ec2_hot{1..4}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid
TEST $CLI volume heal ec2 enable
EXPECT "enable" volume_option ec2 "cluster.disperse-self-heal-daemon"
EXPECT "enable" volgen_volume_option $volfile ec2-disperse-0 cluster disperse self-heal-daemon
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid
TEST $CLI volume heal ec2 disable
EXPECT "disable" volume_option ec2 "cluster.disperse-self-heal-daemon"
EXPECT "disable" volgen_volume_option $volfile ec2-disperse-0 cluster disperse self-heal-daemon
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "[0-9][0-9]*" get_shd_process_pid

#Check that shd graph is rewritten correctly on volume stop/start
EXPECT "Y" volgen_volume_exists $volfile ec2-disperse-0 cluster disperse
EXPECT "Y" volgen_volume_exists $volfile r2-replicate-0 cluster replicate
TEST $CLI volume stop r2
EXPECT "Y" volgen_volume_exists $volfile ec2-disperse-0 cluster disperse
EXPECT "N" volgen_volume_exists $volfile r2-replicate-0 cluster replicate
TEST $CLI volume stop ec2
# When both the volumes are stopped glustershd volfile is not modified just the
# process is stopped
TEST "[ -z $(get_shd_process_pid) ]"

TEST $CLI volume start r2
EXPECT "N" volgen_volume_exists $volfile ec2-disperse-0 cluster disperse
EXPECT "Y" volgen_volume_exists $volfile r2-replicate-0 cluster replicate

TEST $CLI volume start ec2

EXPECT "Y" volgen_volume_exists $volfile ec2-disperse-0 cluster disperse
EXPECT "Y" volgen_volume_exists $volfile ec2-replicate-0 cluster replicate

TEST $CLI volume detach-tier ec2 force

EXPECT "Y" volgen_volume_exists $volfile ec2-disperse-0 cluster disperse
EXPECT "N" volgen_volume_exists $volfile ec2-replicate-0 cluster replicate

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
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
