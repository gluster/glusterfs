#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#Check that operations succeed after changing the disk of the brick while
#a brick is down
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1;
TEST cd $M0
TEST mkdir -p a/b/c/d/e
TEST cd a/b/c/d/e
echo abc > g

#Simulate disk replacement
TEST kill_brick $V0 $H0 $B0/${V0}0
rm -rf $B0/${V0}0/.glusterfs $B0/${V0}0/a

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 0
#Test that the lookup returns ENOENT instead of ESTALE
#If lookup returns ESTALE this command will fail with ESTALE
TEST touch f

#Test that ESTALE is ignored when there is a good copy
EXPECT abc cat g

#Simulate file changing only one mount
#create the file on first mount
echo ghi > $M0/b

#re-create the file on other mount while one of the bricks is down.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST rm -f $M1/b
echo jkl > $M1/b
#Clear the extended attributes on the directory to create a scenario where
#gfid-mismatch happened. This should result in EIO
TEST setfattr -x trusted.afr.$V0-client-0 $B0/${V0}1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 0
# The kernel knows nothing about the tricks done to the volume, and the file
# may still be in page cache. Wait for timeout.
EXPECT_WITHIN 10 "^$" cat $M0/b
cleanup
