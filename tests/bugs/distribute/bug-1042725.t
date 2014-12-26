#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

#Create files
TEST mkdir $M0/foo
TEST touch $M0/foo/{1..20}
for file in {1..20}; do
    ln $M0/foo/$file $M0/foo/${file}_linkfile;
done

#Stop one of the brick
TEST kill_brick ${V0} ${H0} ${B0}/${V0}1

rm -rf $M0/foo 2>/dev/null
TEST stat $M0/foo

touch $M0/foo/{1..20} 2>/dev/null
touch $M0/foo/{1..20}_linkfile 2>/dev/null

TEST $CLI volume start $V0 force;
sleep 5
function verify_duplicate {
    count=`ls $M0/foo | sort | uniq --repeated | grep [0-9] -c`
    echo $count
}
EXPECT 0 verify_duplicate

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
