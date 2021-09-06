#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 client.strict-locks on
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1

TEST touch $M0/a
gfid_a=$(gf_get_gfid_xattr $B0/${V0}0/a)
gfid_str_a=$(gf_gfid_xattr_to_str $gfid_a)


# Open fd from a client, check for open fd on all the bricks.
TEST fd1=`fd_available`
TEST fd_open $fd1 'rw' $M0/a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

# Kill a brick and take lock on the fd
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST flock -x $fd1

# Restart the brick and check for no open fd on the restarted brick.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 0
EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

# Write on the fd. It should fail on the restarted brick.
TEST fd_write $fd1 "data-0"
EXPECT "" cat $B0/${V0}0/a
EXPECT "data-0" cat $B0/${V0}1/a
EXPECT "data-0" cat $B0/${V0}2/a

TEST fd_close $fd1

# Kill one brick and take lock on the fd and do a write.
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST fd1=`fd_available`
TEST fd_open $fd1 'rw' $M0/a

TEST flock -x $fd1
TEST fd_write $fd1 "data-1"

# Restart the brick and then write. Now fd should not get re-opened but write
# should still succeed as there were no quorum disconnects.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST fd_write $fd1 "data-2"
EXPECT "" cat $B0/${V0}0/a
EXPECT "data-2" cat $B0/${V0}1/a
EXPECT "data-2" cat $B0/${V0}2/a

# Check there is no fd opened on the 1st brick by checking for the gfid inside
# /proc/pid-of-brick/fd/ directory
EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

TEST fd2=`fd_available`
TEST fd_open $fd2 'rw' $M1/a

EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^2$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^2$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

# Kill 2nd brick and try writing to the file. The write should fail due to
# quorum failure.
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST ! fd_write $fd1 "data-3"
TEST ! fd_cat $fd1

# Restart the bricks and try writing to the file. This should fail as two bricks
# which were down previously, will return EBADFD now.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST ! fd_write $fd1 "data-4"
TEST ! fd_cat $fd1
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^2$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

# Enable heal and check the files will have same content on all the bricks after
# the heal is completed.
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
EXPECT "data-4" cat $B0/${V0}0/a
EXPECT "data-4" cat $B0/${V0}1/a
EXPECT "data-4" cat $B0/${V0}2/a
TEST $CLI volume heal $V0 disable

# Try writing to the file again on the same fd, which should fail again, since
# it is not yet re-opened.
TEST ! fd_write $fd1 "data-5"

# At this point only one brick will have the lock. Try taking the lock again on
# the bad fd, which should also fail with EBADFD.
# TODO: At the moment quorum failure in lk leads to unlock on the bricks where
# lock succeeds. This will change lock state on 3rd brick, commenting for now
#TEST ! flock -x $fd1

# Kill the only brick that is having lock and try taking lock on another client
# which should succeed.
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_child_up_status_meta $M0 $V0-replicate-0 2
TEST flock -x $fd2
TEST fd_write $fd2 "data-6"
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a


# Bring the brick up and try writing & reading on the old fd, which should still
# fail and operations on the 2nd fd should succeed.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M1 $V0-replicate-0 2
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a
TEST ! fd_write $fd1 "data-7"

TEST ! fd_cat $fd1
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^0" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a
TEST fd_cat $fd2

# Close both the fds which will release the locks and then re-open and take lock
# on the old fd. Operations on that fd should succeed afterwards.
TEST fd_close $fd1
TEST fd_close $fd2

EXPECT_WITHIN $REOPEN_TIMEOUT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT_WITHIN $REOPEN_TIMEOUT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT_WITHIN $REOPEN_TIMEOUT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

TEST fd1=`fd_available`
TEST fd_open $fd1 'rw' $M0/a
EXPECT_WITHIN $REOPEN_TIMEOUT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT_WITHIN $REOPEN_TIMEOUT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT_WITHIN $REOPEN_TIMEOUT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

TEST flock -x $fd1
TEST fd_write $fd1 "data-8"
TEST fd_cat $fd1

EXPECT "data-8" head -n 1 $B0/${V0}0/a
EXPECT "data-8" head -n 1 $B0/${V0}1/a
EXPECT "data-8" head -n 1 $B0/${V0}2/a

TEST fd_close $fd1
EXPECT_WITHIN $REOPEN_TIMEOUT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT_WITHIN $REOPEN_TIMEOUT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT_WITHIN $REOPEN_TIMEOUT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a


# Heal the volume
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
TEST $CLI volume heal $V0 disable

# Kill one brick and open a fd.
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST fd1=`fd_available`
TEST fd_open $fd1 'rw' $M0/a

EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

# Restart the brick and then write. Now fd should get re-opened and write should
# succeed on the previously down brick as well since there are no locks held on
# any of the bricks.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST fd_write $fd1 "data-10"
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a

EXPECT "data-10" head -n 1 $B0/${V0}0/a
EXPECT "data-10" head -n 1 $B0/${V0}1/a
EXPECT "data-10" head -n 1 $B0/${V0}2/a
TEST fd_close $fd1

# Kill one brick, open and take lock on a fd.
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST fd1=`fd_available`
TEST fd_open $fd1 'rw' $M0/a
TEST flock -x $fd1

EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

# Kill & restart another brick so that it will return EBADFD
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" brick_up_status $V0 $H0 $B0/${V0}1

# Restart the bricks and then write. Now fd should not get re-opened since lock
# is still held on one brick and write should also fail as there is no quorum.

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST ! fd_write $fd1 "data-11"
EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}0 $gfid_str_a
EXPECT "^0$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}1 $gfid_str_a
EXPECT "^1$" gf_open_file_count_in_brick $V0 $H0 $B0/${V0}2 $gfid_str_a

EXPECT "data-10" head -n 1 $B0/${V0}0/a
EXPECT "data-10" head -n 1 $B0/${V0}1/a
EXPECT "data-11" head -n 1 $B0/${V0}2/a

TEST fd_close $fd1
cleanup
