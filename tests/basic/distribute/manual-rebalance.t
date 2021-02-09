#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This tests checks if the manual rebalance happens simialr to normal rebalance

TESTS_EXPECTED_IN_LOOP=10
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST mkdir $M0/d
declare -a checksums
for i in {1..10};
do
    TEST_IN_LOOP dd if=/dev/urandom of=$M0/d/$i bs=1M count=1
    checksums[$i]="$(md5sum $M0/d/$i | awk '{print $1}')"
done
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}1 force
TEST $CLI volume rebalance $V0 fix-layout start
EXPECT_WITHIN $REBALANCE_TIMEOUT "fix-layout completed" fix-layout_status_field $V0

errors=0
migrations=0
for i in {1..10};
do
    setfattr -n trusted.distribute.migrate-data -v 1 $M0/d/$i 2>/dev/null
    if [ $? -eq 0 ] #Migration happened for the file
    then
        if [ "${checksums[i]}" != "$(md5sum $B0/${V0}1/d/$i | awk '{print $1}')" ]
        then
            errors=$((errors+1)) #Data on new brick shouldn't change
        else
            migrations=$((migrations+1))
        fi
    else #Migration is not applicable
        if [ "${checksums[i]}" != "$(md5sum $B0/${V0}0/d/$i | awk '{print $1}')" ]
        then
            errors=$((errors+1)) #Data on old brick shouldn't change
        fi
    fi
done

EXPECT_NOT "^0$" echo $migrations #At least one file should migrate
EXPECT "^0$" echo $errors

#Test that rebalance crawl is equivalent to manual rebalance
TEST $CLI volume rebalance $V0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
EXPECT "^0$" rebalanced_files_field $V0


#Do one final check that data didn't change after normal rebalance
success=0
for i in {1..10}
do
    if [ -f $B0/${V0}0/d/$i ]
    then
        if [ "${checksums[i]}" == "$(md5sum $B0/${V0}0/d/$i | awk '{print $1}')" ]
        then
            success=$((success+1))
        fi
    else
        if [ "${checksums[i]}" == "$(md5sum $B0/${V0}1/d/$i | awk '{print $1}')" ]
        then
            success=$((success+1))
        fi
    fi
done

EXPECT "^10$" echo $success
cleanup
