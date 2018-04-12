#!/bin/bash

. $(dirname $0)/../../include.rc


function delete_files {
        local mountpoint=$1;
        local success=0;
        local value=$2
        for i in {1..500}; do
                unlink $mountpoint/file-$i 2>/dev/null 1>/dev/null
                if [ $? -eq 0 ]; then
                        echo $2 >> $B0/output.txt
                fi
        done
        echo $success
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 shard-block-size 4MB
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M1

for i in {1..500}; do
        dd if=/dev/urandom of=$M0/file-$i bs=1M count=2
done

for i in {1..500}; do
        stat $M1/file-$i > /dev/null
done

delete_files $M0 0 &
delete_files $M1 1 &
wait

success1=$(grep 0 $B0/output.txt | wc -l);
success2=$(grep 1 $B0/output.txt | wc -l);

echo "Success1 is $success1";
echo "Success2 is $success2";

success_total=$((success1 + success2));

EXPECT 500 echo $success_total

cleanup
