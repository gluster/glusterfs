#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

## Lets create partitions for bricks
TEST truncate -s 100M $B0/brick1
TEST truncate -s 200M $B0/brick2
TEST truncate -s 200M $B0/brick3
TEST truncate -s 200M $B0/brick4


TEST LO1=`SETUP_LOOP $B0/brick1`
TEST LO2=`SETUP_LOOP $B0/brick2`
TEST LO3=`SETUP_LOOP $B0/brick3`
TEST LO4=`SETUP_LOOP $B0/brick4`


TEST MKFS_LOOP $LO1
TEST MKFS_LOOP $LO2
TEST MKFS_LOOP $LO3
TEST MKFS_LOOP $LO4

TEST mkdir -p ${B0}/${V0}{0..3}


TEST MOUNT_LOOP $LO1 $B0/${V0}0
TEST MOUNT_LOOP $LO2 $B0/${V0}1
TEST MOUNT_LOOP $LO3 $B0/${V0}2
TEST MOUNT_LOOP $LO4 $B0/${V0}3


TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers;


CLI_1_WITHOUT_WIGNORE=$(echo $CLI_1 | sed 's/ --wignore//')


# Creating volume with non resolvable host name
TEST ! $CLI_1_WITHOUT_WIGNORE volume create $V0 replica 2 \
        $H1:$B0/${V0}0/brick redhat:$B0/${V0}1/brick \
        $H1:$B0/${V0}2/brick redhat:$B0/${V0}3/brick


#Workaround for Bug:1091935
#Failure to create volume above leaves 1st brick with xattrs.
rm -rf $B0/${V0}{0..3}/brick;


# Creating distribute-replica volume with bad brick order. It will fail
# due to bad brick order.
TEST ! $CLI_1_WITHOUT_WIGNORE volume create $V0 replica 2 \
        $H1:$B0/${V0}0/brick $H1:$B0/${V0}1/brick \
        $H1:$B0/${V0}2/brick $H1:$B0/${V0}3/brick



#Workaround for Bug:1091935
#Failure to create volume above leaves 1st brick with xattrs.
rm -rf $B0/${V0}{0..3}/brick;



# Test for positive case, volume create should pass for
# resolved hostnames and bricks in order.
TEST $CLI_1_WITHOUT_WIGNORE volume create $V0 replica 2 \
        $H1:$B0/${V0}0/brick $H2:$B0/${V0}1/brick \
        $H1:$B0/${V0}2/brick $H2:$B0/${V0}3/brick

# Delete the volume as we want to reuse bricks
TEST $CLI_1_WITHOUT_WIGNORE volume delete $V0


# Now with force at the end of command it will bypass brick-order check
# for replicate or distribute-replicate volume. and it will create volume
TEST $CLI_1_WITHOUT_WIGNORE volume create $V0 replica 2 \
        $H1:$B0/${V0}0/brick $H1:$B0/${V0}1/brick \
        $H1:$B0/${V0}2/brick $H1:$B0/${V0}3/brick force

# Need to cleanup the loop back devices.
UMOUNT_LOOP ${B0}/${V0}{0..3}
rm -f ${B0}/brick{1..4}

cleanup;
