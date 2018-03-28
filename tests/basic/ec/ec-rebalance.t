#!/bin/bash
#
# This will test the rebalance failure reported in 1447559
#
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

cleanup

#cleate and start volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 lookup-optimize on
TEST $CLI volume start $V0

#Mount the volume
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

# Create files
for i in {1..10}
do
    dd if=/dev/urandom of=$M0/file$i bs=1024k count=1
done

md5_1=$(md5sum $M0/file1 | awk '{print $1}')
md5_2=$(md5sum $M0/file2 | awk '{print $1}')
md5_3=$(md5sum $M0/file3 | awk '{print $1}')
md5_4=$(md5sum $M0/file4 | awk '{print $1}')
md5_5=$(md5sum $M0/file5 | awk '{print $1}')
md5_6=$(md5sum $M0/file6 | awk '{print $1}')
md5_7=$(md5sum $M0/file7 | awk '{print $1}')
md5_8=$(md5sum $M0/file8 | awk '{print $1}')
md5_9=$(md5sum $M0/file9 | awk '{print $1}')
md5_10=$(md5sum $M0/file10 | awk '{print $1}')
# Add brick
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{3..5}

#Trigger rebalance
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

#Remount to avoid any caches
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT "$md5_1" echo $(md5sum $M0/file1 | awk '{print $1}')
EXPECT "$md5_2" echo $(md5sum $M0/file2 | awk '{print $1}')
EXPECT "$md5_3" echo $(md5sum $M0/file3 | awk '{print $1}')
EXPECT "$md5_4" echo $(md5sum $M0/file4 | awk '{print $1}')
EXPECT "$md5_5" echo $(md5sum $M0/file5 | awk '{print $1}')
EXPECT "$md5_6" echo $(md5sum $M0/file6 | awk '{print $1}')
EXPECT "$md5_7" echo $(md5sum $M0/file7 | awk '{print $1}')
EXPECT "$md5_8" echo $(md5sum $M0/file8 | awk '{print $1}')
EXPECT "$md5_9" echo $(md5sum $M0/file9 | awk '{print $1}')
EXPECT "$md5_10" echo $(md5sum $M0/file10 | awk '{print $1}')

cleanup;
