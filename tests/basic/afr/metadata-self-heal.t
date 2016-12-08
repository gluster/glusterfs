#!/bin/bash
#Self-heal tests

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

function is_heal_done {
        local f1_path="${1}/${3}"
        local f2_path="${2}/${3}"
        local zero_xattr="000000000000000000000000"
        local iatt1=$(stat -c "%g:%u:%A" $f1_path)
        local iatt2=$(stat -c "%g:%u:%A" $f2_path)
        local diff="1"
        if [ "$iatt1" == "$iatt2" ]; then diff=0; fi
        local xattr11=$(get_hex_xattr trusted.afr.$V0-client-0 $f1_path)
        local xattr12=$(get_hex_xattr trusted.afr.$V0-client-1 $f1_path)
        local xattr21=$(get_hex_xattr trusted.afr.$V0-client-0 $f2_path)
        local xattr22=$(get_hex_xattr trusted.afr.$V0-client-1 $f2_path)
        local dirty1=$(get_hex_xattr trusted.afr.dirty $f1_path)
        local dirty2=$(get_hex_xattr trusted.afr.dirty $f2_path)
        if [ -z $xattr11 ]; then xattr11="000000000000000000000000"; fi
        if [ -z $xattr12 ]; then xattr12="000000000000000000000000"; fi
        if [ -z $xattr21 ]; then xattr21="000000000000000000000000"; fi
        if [ -z $xattr22 ]; then xattr22="000000000000000000000000"; fi
        if [ -z $dirty1 ]; then dirty1="000000000000000000000000"; fi
        if [ -z $dirty2 ]; then dirty2="000000000000000000000000"; fi
        if [ "${diff}${xattr11}${xattr12}${xattr21}${xattr22}${dirty1}${dirty2}" == "0${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" ];
        then
                echo "Y"
        else
                echo "N"
        fi
}

function print_pending_heals {
        local result=":"
        for i in "$@";
        do
                if [ "N" == $(is_heal_done $B0/brick0 $B0/brick1 $i) ];
                then
                        result="$result:$i"
                fi
        done
#To prevent any match for EXPECT_WITHIN, print a char non-existent in file-names
        if [ $result == ":" ]; then result="~"; fi
        echo $result
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
cd $M0

TEST touch a

#Test heal with pending xattrs
TEST kill_brick $V0 $H0 $B0/brick0
TEST chmod 777 a
TEST chown 100:100 a
TEST setfattr -n trusted.abc -v 0x616263 a
TEST setfattr -n trusted.def -v 0x646566 a
permissions=$(stat -c "%A" a)
TEST $CLI volume start $V0 force
EXPECT_WITHIN $HEAL_TIMEOUT "~" print_pending_heals a
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

EXPECT $permissions stat -c "%A" a
EXPECT $permissions stat -c "%A" $B0/brick0/a
EXPECT $permissions stat -c "%A" $B0/brick1/a

EXPECT 100 stat -c "%g" a
EXPECT 100 stat -c "%g" $B0/brick0/a
EXPECT 100 stat -c "%g" $B0/brick1/a

EXPECT 100 stat -c "%u" a
EXPECT 100 stat -c "%u" $B0/brick0/a
EXPECT 100 stat -c "%u" $B0/brick1/a

EXPECT 616263 get_hex_xattr trusted.abc a
EXPECT 616263 get_hex_xattr trusted.abc $B0/brick0/a
EXPECT 616263 get_hex_xattr trusted.abc $B0/brick1/a

EXPECT 646566 get_hex_xattr trusted.def a
EXPECT 646566 get_hex_xattr trusted.def $B0/brick0/a
EXPECT 646566 get_hex_xattr trusted.def $B0/brick1/a

TEST kill_brick $V0 $H0 $B0/brick1
TEST setfattr -n trusted.abc -v 0x646566 a
TEST setfattr -x trusted.def a

TEST $CLI volume start $V0 force
EXPECT_WITHIN $HEAL_TIMEOUT "~" print_pending_heals a
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

EXPECT 646566 get_hex_xattr trusted.abc a
EXPECT 646566 get_hex_xattr trusted.abc $B0/brick0/a
EXPECT 646566 get_hex_xattr trusted.abc $B0/brick1/a

TEST ! getfattr -n trusted.def a
TEST ! getfattr -n trusted.def $B0/brick0/a
TEST ! getfattr -n trusted.def $B0/brick1/a

#Test split-brain && iatt mismatch without any xattrs (this will be simulated)
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST touch b
TEST touch c
TEST kill_brick $V0 $H0 $B0/brick0
TEST chmod 777 b
TEST chmod 777 c
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/brick1
TEST chown 100:100 b
TEST chown 100:100 c
TEST $CLI volume stop $V0
TEST setfattr -x trusted.afr.$V0-client-0 $B0/brick1/c
TEST setfattr -x trusted.afr.$V0-client-1 $B0/brick0/c
TEST $CLI volume set $V0 cluster.self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "~" print_pending_heals c
EXPECT "N" is_heal_done $B0/brick0 $B0/brick1 b

cd -
cleanup;
