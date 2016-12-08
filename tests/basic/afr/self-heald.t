#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
START_TIMESTAMP=`date +%s`

function kill_multiple_bricks {
        local vol=$1
        local host=$2
        local brickpath=$3

        if [ $decide_kill == 0 ]
        then
                for ((i=0; i<=4; i=i+2)) do
                        TEST kill_brick $vol $host $brickpath/${vol}$i
                done
        else
                for ((i=1; i<=5; i=i+2)) do
                        TEST kill_brick $vol $host $brickpath/${vol}$i
                done
        fi
}
function check_bricks_up {
        local vol=$1
        if [ $decide_kill == 0 ]
        then
                for ((i=0; i<=4; i=i+2)) do
                        EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status_in_shd $vol $i
                done
        else
                for ((i=1; i<=5; i=i+2)) do
                        EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status_in_shd $vol $i
                done
        fi
}

function disconnected_brick_count {
        local vol=$1
        $CLI volume heal $vol info | \
            egrep -i '(transport|Socket is not connected)' | wc -l
}

TESTS_EXPECTED_IN_LOOP=20
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

decide_kill=$((`date +"%j"|sed 's/^0*//'` % 2 ))

kill_multiple_bricks $V0 $H0 $B0
cd $M0
HEAL_FILES=0
for i in {1..10}
do
        dd if=/dev/urandom of=f bs=1024k count=10
        HEAL_FILES=$(($HEAL_FILES+1)) #+1 for data/metadata self-heal of 'f'
        mkdir a; cd a;
        #+3 for metadata self-heal of 'a' one per subvolume of DHT
        HEAL_FILES=$(($HEAL_FILES+3))
done
#+3 represents entry sh on "/", one per subvolume of DHT?
HEAL_FILES=$(($HEAL_FILES + 3))

cd ~
EXPECT "$HEAL_FILES" get_pending_heal_count $V0

#When bricks are down, it says Transport End point Not connected for them
EXPECT "3" disconnected_brick_count $V0

#Create some stale indices and verify that they are not counted in heal info
#TO create stale index create and delete files when one brick is down in
#replica pair.
for i in {11..20}; do echo abc > $M0/$i; done
HEAL_FILES=$(($HEAL_FILES + 10)) #count extra 10 files
EXPECT "$HEAL_FILES" get_pending_heal_count $V0
#delete the files now, so that stale indices will remain.
for i in {11..20}; do rm -f $M0/$i; done
#After deleting files they should not appear in heal info
HEAL_FILES=$(($HEAL_FILES - 10))
EXPECT "$HEAL_FILES" get_pending_heal_count $V0


TEST ! $CLI volume heal $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST ! $CLI volume heal $V0
TEST ! $CLI volume heal $V0 full
TEST $CLI volume start $V0 force
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status

check_bricks_up $V0

TEST $CLI volume heal $V0
sleep 5 #Until the heal-statistics command implementation
#check that this heals the contents partially
TEST [ $HEAL_FILES -gt $(get_pending_heal_count $V0) ]

TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#Test that ongoing IO is not considered as Pending heal
(dd if=/dev/zero of=$M0/file1 bs=1k 2>/dev/null 1>/dev/null)&
back_pid1=$!;
(dd if=/dev/zero of=$M0/file2 bs=1k 2>/dev/null 1>/dev/null)&
back_pid2=$!;
(dd if=/dev/zero of=$M0/file3 bs=1k 2>/dev/null 1>/dev/null)&
back_pid3=$!;
(dd if=/dev/zero of=$M0/file4 bs=1k 2>/dev/null 1>/dev/null)&
back_pid4=$!;
(dd if=/dev/zero of=$M0/file5 bs=1k 2>/dev/null 1>/dev/null)&
back_pid5=$!;
EXPECT 0 get_pending_heal_count $V0
kill -SIGTERM $back_pid1;
kill -SIGTERM $back_pid2;
kill -SIGTERM $back_pid3;
kill -SIGTERM $back_pid4;
kill -SIGTERM $back_pid5;
wait >/dev/null 2>&1;

#Test that volume heal info reports files even when self-heal
#options are disabled
TEST touch $M0/f
TEST mkdir $M0/d
#DATA
TEST $CLI volume set $V0 cluster.data-self-heal off
EXPECT "off" volume_option $V0 cluster.data-self-heal
kill_multiple_bricks $V0 $H0 $B0
echo abc > $M0/f
EXPECT 1 get_pending_heal_count $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
check_bricks_up $V0

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
TEST $CLI volume set $V0 cluster.data-self-heal on

#METADATA
TEST $CLI volume set $V0 cluster.metadata-self-heal off
EXPECT "off" volume_option $V0 cluster.metadata-self-heal
kill_multiple_bricks $V0 $H0 $B0

TEST chmod 777 $M0/f
EXPECT 1 get_pending_heal_count $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
check_bricks_up $V0

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
TEST $CLI volume set $V0 cluster.metadata-self-heal on

#ENTRY
TEST $CLI volume set $V0 cluster.entry-self-heal off
EXPECT "off" volume_option $V0 cluster.entry-self-heal
kill_multiple_bricks $V0 $H0 $B0
TEST touch $M0/d/a
# 4 if mtime/ctime is modified for d in bricks without a
# 2 otherwise
PENDING=$( get_pending_heal_count $V0 )
TEST test $PENDING -eq 2 -o $PENDING -eq 4
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
check_bricks_up $V0
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
TEST $CLI volume set $V0 cluster.entry-self-heal on

#Negative test cases
#Fail volume does not exist case
TEST ! $CLI volume heal fail info

#Fail volume stopped case
TEST $CLI volume stop $V0
TEST ! $CLI volume heal $V0 info

#Fail non-replicate volume info
TEST $CLI volume delete $V0
TEST $CLI volume create $V0 $H0:$B0/${V0}{6}
TEST $CLI volume start $V0
TEST ! $CLI volume heal $V0 info

# Check for non Linux systems that we did not mess with directory offsets
TEST ! log_newer $START_TIMESTAMP "offset reused from another DIR"

cleanup
