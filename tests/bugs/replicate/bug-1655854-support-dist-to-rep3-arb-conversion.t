#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd

# Conversion from 2x1 to 2x3

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST mkdir $M0/dir
TEST dd if=/dev/urandom of=$M0/dir/file bs=100K count=5
file_md5sum=$(md5sum $M0/dir/file | awk '{print $1}')

TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}{2..5}

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}5

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 4
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 5

# Trigger heal and wait for for it to complete
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Check whether the directory & file are healed to the newly added bricks
TEST ls $B0/${V0}2/dir
TEST ls $B0/${V0}3/dir
TEST ls $B0/${V0}4/dir
TEST ls $B0/${V0}5/dir

TEST [ $file_md5sum == $(md5sum $B0/${V0}4/dir/file | awk '{print $1}') ]
TEST [ $file_md5sum == $(md5sum $B0/${V0}5/dir/file | awk '{print $1}') ]


# Conversion from 2x1 to 2x(2+1)

TEST $CLI volume create $V1 $H0:$B0/${V1}{0,1}
EXPECT 'Created' volinfo_field $V1 'Status';
TEST $CLI volume start $V1
EXPECT 'Started' volinfo_field $V1 'Status';

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}1

TEST $GFS --volfile-id=$V1 --volfile-server=$H0 $M1;
TEST mkdir $M1/dir
TEST dd if=/dev/urandom of=$M1/dir/file bs=100K count=5
file_md5sum=$(md5sum $M1/dir/file | awk '{print $1}')

TEST $CLI volume add-brick $V1 replica 3 arbiter 1 $H0:$B0/${V1}{2..5}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}5

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V1 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V1 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V1 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V1 3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V1 4
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V1 5

# Trigger heal and wait for for it to complete
TEST $CLI volume heal $V1
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V1

# Check whether the directory & file are healed to the newly added bricks
TEST ls $B0/${V1}2/dir
TEST ls $B0/${V1}3/dir
TEST ls $B0/${V1}4/dir
TEST ls $B0/${V1}5/dir

EXPECT "0" stat -c %s $B0/${V1}5/dir/file
TEST [ $file_md5sum == $(md5sum $B0/${V1}4/dir/file | awk '{print $1}') ]

cleanup;
