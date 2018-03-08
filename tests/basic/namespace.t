#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

# These hashes are a result of calling SuperFastHash
# on the corresponding folder names.
NAMESPACE_HASH=28153613
NAMESPACE2_HASH=3926991974
NAMESPACE3_HASH=3493960770

function check_brick_multiplex() {
        local ret=$($CLI volume info|grep "cluster.brick-multiplex"|cut -d" " -f2)
        local cnt="$(ls /var/log/glusterfs/bricks|wc -l)"
        local bcnt="$(brick_count)"

        if [ $bcnt -ne 1 ]; then
           if [ -z $ret ]; then
              ret="no"
           fi

           if [ $ret = "on" ] || [ $cnt -eq 1 ]; then
              echo "Y"
           else
              echo "N"
           fi
        else
           echo "N"
        fi
}

function check_samples() {
        local FOP_TYPE=$1
        local NS_HASH=$2
        local FILE=$3
        local BRICK=$4
        local GFID="$(getfattr -n trusted.gfid -e text --only-values $B0/$BRICK$FILE | xxd -p)"
        local val="$(check_brick_multiplex)"

        if [ $val = "Y" ]; then
           BRICK="${V0}0"
        fi

        grep -i "ns_$OP" /var/log/glusterfs/bricks/d-backends-$BRICK.log |
             grep -- $NS_HASH | sed 's/\-//g' | grep -- $GFID
        if [ $? -eq 0 ]; then
          echo "Y"
         else
          echo "N"
        fi
}

cleanup;

TEST mkdir -p $B0/${V0}{0,1,2,3,4,5,6,7,8,9}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5,6,7,8}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.nfs.stat-prefetch off
TEST $CLI volume set $V0 cluster.read-subvolume-index 0
TEST $CLI volume set $V0 diagnostics.brick-log-level DEBUG
TEST $CLI volume set $V0 features.tag-namespaces on
TEST $CLI volume set $V0 storage.build-pgfid on
TEST $CLI volume start $V0

sleep 2

TEST mount_nfs $H0:/$V0 $N0 nolock;

################################
# Paths in the samples #
################################

mkdir -p $N0/namespace

# subvol_1 = bar, subvol_2 = foo, subvol_3 = hey
# Test create, write (tagged by loc, fd respectively).
touch $N0/namespace/{bar,foo,hey}
echo "garbage" > $N0/namespace/bar
echo "garbage" > $N0/namespace/foo
echo "garbage" > $N0/namespace/hey
EXPECT_WITHIN 10 "Y" check_samples CREATE $NAMESPACE_HASH /namespace/bar patchy0
EXPECT_WITHIN 10 "Y" check_samples CREATE $NAMESPACE_HASH /namespace/foo patchy3
EXPECT_WITHIN 10 "Y" check_samples CREATE $NAMESPACE_HASH /namespace/hey patchy6
EXPECT_WITHIN 10 "Y" check_samples WRITEV $NAMESPACE_HASH /namespace/bar patchy0
EXPECT_WITHIN 10 "Y" check_samples WRITEV $NAMESPACE_HASH /namespace/foo patchy3
EXPECT_WITHIN 10 "Y" check_samples WRITEV $NAMESPACE_HASH /namespace/hey patchy6

# Test stat (tagged by loc)
stat $N0/namespace/bar &> /dev/null
stat $N0/namespace/foo &> /dev/null
stat $N0/namespace/hey &> /dev/null
EXPECT_WITHIN 10 "Y" check_samples STAT $NAMESPACE_HASH /namespace/bar patchy0
EXPECT_WITHIN 10 "Y" check_samples STAT $NAMESPACE_HASH /namespace/foo patchy3
EXPECT_WITHIN 10 "Y" check_samples STAT $NAMESPACE_HASH /namespace/hey patchy6

EXPECT_WITHIN 10 "Y" umount_nfs $N0;
sleep 1
TEST mount_nfs $H0:/$V0 $N0 nolock;

cat $N0/namespace/bar &> /dev/null
EXPECT_WITHIN 10 "Y" check_samples READ $NAMESPACE_HASH /namespace/bar patchy0

dir $N0/namespace &> /dev/null
EXPECT_WITHIN 10 "Y" check_samples LOOKUP $NAMESPACE_HASH /namespace patchy0

mkdir -p $N0/namespace{2,3}
EXPECT_WITHIN 10 "Y" check_samples MKDIR $NAMESPACE2_HASH /namespace2 patchy0
EXPECT_WITHIN 10 "Y" check_samples MKDIR $NAMESPACE3_HASH /namespace3 patchy0

touch $N0/namespace2/file
touch $N0/namespace3/file
EXPECT_WITHIN 10 "Y" check_samples CREATE $NAMESPACE2_HASH /namespace2/file patchy0
EXPECT_WITHIN 10 "Y" check_samples CREATE $NAMESPACE3_HASH /namespace3/file patchy0

truncate -s 0 $N0/namespace/bar
EXPECT_WITHIN 10 "Y" check_samples TRUNCATE $NAMESPACE_HASH /namespace/bar patchy0

ln -s $N0/namespace/foo $N0/namespace/foo_link
EXPECT_WITHIN 10 "Y" check_samples SYMLINK $NAMESPACE_HASH /namespace/foo patchy3

open $N0/namespace/hey
EXPECT_WITHIN 10 "Y" check_samples OPEN $NAMESPACE_HASH /namespace/hey patchy6

cleanup;
