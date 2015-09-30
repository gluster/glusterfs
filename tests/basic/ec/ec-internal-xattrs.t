#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks internal xattr handling in ec
TESTS_EXPECTED_IN_LOOP=11

cleanup
function get_ec_xattrs
{
        getfattr -d -m. -e hex $1 | grep trusted.ec
}

function get_xattr_count
{
        getfattr -d -m. -e hex $1 | grep "trusted" | wc -l
}

declare -a xattrs=("trusted.ec.config" "trusted.ec.size" "trusted.ec.version" "trusted.ec.heal")

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
# Wait until all 6 children have been recognized by the ec xlator
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
TEST touch $M0/a

#Check that internal xattrs are not modifiable or readable
for x in "${xattrs[@]}"; do
        TEST_IN_LOOP ! setfattr -n $x "abc" $M0/a
        TEST_IN_LOOP ! setfattr -x $x "abc" $M0/a
        if [ $x != "trusted.ec.heal" ];
        then
                TEST_IN_LOOP ! getfattr -n $x $M0/a
        fi
done

TEST ! get_ec_xattrs $M0/a
TEST setfattr -n trusted.abc -v 0x616263 $M0/a
EXPECT "616263" get_hex_xattr trusted.abc $M0/a
EXPECT "1" get_xattr_count $M0/a
cleanup
