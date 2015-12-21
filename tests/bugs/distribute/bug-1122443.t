#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

make_files() {
    mkdir $1 && \
    ln -s ../ $1/symlink && \
    mknod $1/special_b b 1 2 && \
    mknod $1/special_c c 3 4 && \
    mknod $1/special_u u 5 6 && \
    mknod $1/special_p p && \
    touch -h --date=@1 $1/symlink && \
    touch -h --date=@2 $1/special_b &&
    touch -h --date=@3 $1/special_c &&
    touch -h --date=@4 $1/special_u &&
    touch -h --date=@5 $1/special_p
}

bug_1113050_workaround() {
    # Test if graph change has settled (bug-1113050?)
    test=$(stat -c "%n:%Y" $1 2>&1 | tr '\n' ',')
    if [ $? -eq 0 ] ; then
	echo RECONNECTED
    else
	echo WAITING
    fi
    return 0
}

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0

# Mount FUSE and create symlink
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST make_files $M0/subdir

# Get mtime before migration
BEFORE="$(stat -c %n:%Y $M0/subdir/* | tr '\n' ',')"

# Migrate brick
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}1
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0 $H0:$B0/${V0}0"
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}0 commit

# Get mtime after migration
EXPECT_WITHIN 5 RECONNECTED bug_1113050_workaround $M0/subdir/*
AFTER="$(stat -c %n:%Y $M0/subdir/* | tr '\n' ',')"

# Check if mtime is unchanged
TEST [ "$AFTER" == "$BEFORE" ]

cleanup
