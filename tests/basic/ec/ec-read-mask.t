 #!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../ec.rc

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0

#Empty read-mask should fail
TEST ! $GFS --xlator-option=*.ec-read-mask="" -s $H0 --volfile-id $V0 $M0

#Less than 4 number of bricks should fail
TEST ! $GFS --xlator-option="*.ec-read-mask=0" -s $H0 --volfile-id $V0 $M0
TEST ! $GFS --xlator-option="*.ec-read-mask=0:1" -s $H0 --volfile-id $V0 $M0
TEST ! $GFS --xlator-option=*.ec-read-mask="0:1:2" -s $H0 --volfile-id $V0 $M0

#ids greater than 5 should fail
TEST ! $GFS --xlator-option="*.ec-read-mask=0:1:2:6" -s $H0 --volfile-id $V0 $M0

#ids less than 0 should fail
TEST ! $GFS --xlator-option="*.ec-read-mask=0:-1:2:5" -s $H0 --volfile-id $V0 $M0

#read-mask with non-alphabet or comma should fail
TEST ! $GFS --xlator-option="*.ec-read-mask=0:1:2:5:abc" -s $H0 --volfile-id $V0 $M0
TEST ! $GFS --xlator-option="*.ec-read-mask=0:1:2:5a" -s $H0 --volfile-id $V0 $M0

#mount with at least 4 read-mask-ids and all of them valid should pass
TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5:4:3" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^111111$" ec_option_value $V0 $M0 0 read-mask
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask

TEST dd if=/dev/urandom of=$M0/a bs=1M count=1
md5=$(md5sum $M0/a | awk '{print $1}')
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Read on the file should fail if any of the read-mask is down when number of
#ids is data-count
TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST ! dd if=$M0/a of=/dev/null
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! dd if=$M0/a of=/dev/null
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST ! dd if=$M0/a of=/dev/null
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}5
TEST ! dd if=$M0/a of=/dev/null
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

#Read on file should succeed when non-read-mask bricks are down
TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT "^$md5$" echo $(dd if=$M0/a | md5sum | awk '{print $1}')
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}4
EXPECT "^$md5$" echo $(dd if=$M0/a | md5sum | awk '{print $1}')
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST kill_brick $V0 $H0 $B0/${V0}4
EXPECT "^$md5$" echo $(dd if=$M0/a | md5sum | awk '{print $1}')
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume start $V0 force

#Deliberately corrupt chunks 3: 4 and check that reads still give correct data
TEST dd if=/dev/zero of=$B0/${V0}3/a bs=256k count=1
TEST dd if=/dev/zero of=$B0/${V0}4/a bs=256k count=1
TEST $GFS --xlator-option="*.ec-read-mask=0:1:2:5" -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT "^100111$" ec_option_value $V0 $M0 0 read-mask
EXPECT "^$md5$" echo $(dd if=$M0/a | md5sum | awk '{print $1}')
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup;
