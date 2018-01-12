#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks if heal info works as expected or not

function create_files {
        for i in {21..1000};
        do
                dd if=/dev/zero of=$M0/$i bs=1M count=1 2>&1 > /dev/null;
        done
        rm -f $M0/lock
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume set $V0 client-log-level DEBUG
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --direct-io-mode=yes $M0;
# Wait until all 6 childs have been recognized by the ec xlator
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#heal info should give zero entries to be healed when I/O is going on
dd if=/dev/zero of=$M0/a bs=1M count=2048 &
dd_pid=$!
sleep 3 #Wait for I/O to proceed for some time
EXPECT "^0$" get_pending_heal_count $V0
kill -9 $dd_pid
touch $M0/lock
create_files &

total_heal_count=0
while [ -f $M0/lock ];
do
        heal_count=$(get_pending_heal_count $V0)
        total_heal_count=$((heal_count+total_heal_count))
done
EXPECT "^0$" echo $total_heal_count

#When only data heal is required it should print it
#There is no easy way to create this using commands so assigning xattrs directly
TEST setfattr -n trusted.ec.version -v 0x00000000000000020000000000000000 $B0/${V0}0/1000
TEST setfattr -n trusted.ec.version -v 0x00000000000000020000000000000000 $B0/${V0}1/1000
TEST setfattr -n trusted.ec.version -v 0x00000000000000020000000000000000 $B0/${V0}2/1000
TEST setfattr -n trusted.ec.version -v 0x00000000000000020000000000000000 $B0/${V0}3/1000
TEST setfattr -n trusted.ec.version -v 0x00000000000000020000000000000000 $B0/${V0}4/1000
TEST setfattr -n trusted.ec.version -v 0x00000000000000010000000000000000 $B0/${V0}5/1000
index_path=$B0/${V0}5/.glusterfs/indices/xattrop/$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}5/1000))
while [ -f $index_path ]; do :; done
TEST touch $index_path
EXPECT "^1$" get_pending_heal_count $V0
TEST rm -f $M0/1000

#When files/directories need heal test that it prints them
TEST touch $M0/{1..10}
TEST kill_brick $V0 $H0 $B0/${V0}0
for i in {11..20};
do
        echo abc > $M0/$i #Data + entry + metadata heal
done
for i in {1..10};
do
        chmod +x $M0/$i;
done

EXPECT "^105$" get_pending_heal_count $V0

cleanup
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1533815
