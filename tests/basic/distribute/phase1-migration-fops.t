#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

#This test checks if the fops happen on both hashed and cached subvolume when
#the migration is in progress

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.delay-duration 10000000 #10 seconds
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable read #migration should be slow
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1
rebalanced_file=""
cached_file=""

# Launch a dd with 1M block count to make sure io-threads will launch enough
# threads to makesure when READ is blocked, there are other threads that will
# pick up the other fops.
dd if=/dev/zero of=$M0/a bs=1M count=10 &
dd if=/dev/zero of=$M0/b bs=1M count=10 &
wait
for i in {1..10};
do
    echo abc > $M0/$i;
    mv $M0/$i $M0/${i}.txt #Hoping at least one file will hash to another subvolume after rename
    if [ -f $B0/${V0}0/${i}.txt ] && [ -f $B0/${V0}1/${i}.txt ];
    then
        if [ ! -k $B0/${V0}0/${i}.txt ] #Checks the file doesn't have sticky bit
        then
            cached_file=$B0/${V0}0/${i}.txt
        else
            cached_file=$B0/${V0}1/${i}.txt
        fi
        rebalanced_file=${i}.txt
        setfattr -n trusted.distribute.migrate-data -v force $M0/$rebalanced_file &
        break;
    fi
done

TEST [ ! -z $rebalanced_file ];
TEST [ ! -z $cached_file ];

EXPECT_WITHIN $MIGRATION_START_TIMEOUT "Y" has_sticky_attr $cached_file
EXPECT_WITHIN $MIGRATION_START_TIMEOUT "Y" has_sgid_attr $cached_file
TEST $CLI volume profile $V0 start
echo abc >> $M1/$rebalanced_file
EXPECT '8' stat -c %s $M1/$rebalanced_file
TEST truncate -s 0 $M1/$rebalanced_file
EXPECT '0' stat -c %s $M1/$rebalanced_file
#TEST chmod +x $M1/$rebalanced_file
#fallocate
TEST fallocate -l 1M $M1/$rebalanced_file
#discard
TEST fallocate -p -o 512k -l 128k $M1/$rebalanced_file
#Clear profile info incremental
TEST $CLI volume profile $V0 info incremental
fallocate_count=$($CLI volume profile $V0 info | grep FALLOCATE | wc -l)
discard_count=$($CLI volume profile $V0 info | grep DISCARD | wc -l)
ftruncate_count=$($CLI volume profile $V0 info | grep FTRUNCATE | wc -l)
write_count=$($CLI volume profile $V0 info | grep WRITE | wc -l)
TEST wait

#Fops should go to both the bricks because migration is in phase-1
EXPECT "^2$" echo $fallocate_count
EXPECT "^2$" echo $discard_count
EXPECT "^2$" echo $ftruncate_count
EXPECT "^2$" echo $write_count
cleanup
