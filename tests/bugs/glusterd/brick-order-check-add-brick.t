#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST verify_lvm_version;
#Create cluster with 3 nodes
TEST launch_cluster 3 -NO_DEBUG -NO_FORCE
TEST setup_lvm 3

TEST $CLI_1 peer probe $H2
TEST $CLI_1 peer probe $H3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 volume create $V0 replica 3 $H1:$L1/$V0 $H2:$L2/$V0 $H3:$L3/$V0
EXPECT '1 x 3 = 3' volinfo_field $V0 'Number of Bricks'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

#add-brick with or without mentioning the replica count should not fail
TEST $CLI_1 volume add-brick $V0 replica 3 $H1:$L1/${V0}_1 $H2:$L2/${V0}_1 $H3:$L3/${V0}_1
EXPECT '2 x 3 = 6' volinfo_field $V0 'Number of Bricks'

TEST $CLI_1 volume add-brick $V0 $H1:$L1/${V0}_2 $H2:$L2/${V0}_2 $H3:$L3/${V0}_2
EXPECT '3 x 3 = 9' volinfo_field $V0 'Number of Bricks'

#adding bricks from same host should fail the brick order check
TEST ! $CLI_1 volume add-brick $V0 $H1:$L1/${V0}_3 $H1:$L1/${V0}_4 $H1:$L1/${V0}_5
EXPECT '3 x 3 = 9' volinfo_field $V0 'Number of Bricks'

#adding bricks from same host with force should succeed
TEST $CLI_1 volume add-brick $V0 $H1:$L1/${V0}_3 $H1:$L1/${V0}_4 $H1:$L1/${V0}_5 force
EXPECT '4 x 3 = 12' volinfo_field $V0 'Number of Bricks'

TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume delete $V0

TEST $CLI_1 volume create $V0 replica 2 $H1:$L1/${V0}1 $H2:$L2/${V0}1
EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

#Add-brick with Increasing replica count
TEST $CLI_1 volume add-brick $V0 replica 3 $H3:$L3/${V0}1
EXPECT '1 x 3 = 3' volinfo_field $V0 'Number of Bricks'

#Add-brick with Increasing replica count from same host should fail
TEST ! $CLI_1 volume add-brick $V0 replica 5 $H1:$L1/${V0}2 $H1:$L1/${V0}3

#adding multiple bricks from same host should fail the brick order check
TEST ! $CLI_1 volume add-brick $V0 replica 3 $H1:$L1/${V0}{4..6} $H2:$L2/${V0}{7..9}
EXPECT '1 x 3 = 3' volinfo_field $V0 'Number of Bricks'

cleanup
