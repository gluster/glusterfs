#!/bin/bash

## Regression test for https://github.com/gluster/glusterfs/issues/4850
## Writing through a MAP_SHARED mapping on a file opened O_APPEND used
## to append at EOF (wrong offset) and double the file size, because
## the brick's pwrite() honored the backend fd's O_APPEND. write-behind
## also dropped the request xdata that carries the positional marker.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

V2=patchy2

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## distribute volume
TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

## replica volume
TEST $CLI volume create $V1 replica 2 $H0:$B0/brick2 $H0:$B0/brick3;
EXPECT 'Created' volinfo_field $V1 'Status';

## shard volume
TEST $CLI volume create $V2 $H0:$B0/brick4;
TEST $CLI volume set $V2 features.shard-block-size 4MB;
TEST $CLI volume set $V2 features.shard on;
EXPECT 'Created' volinfo_field $V2 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume start $V1;
EXPECT 'Started' volinfo_field $V1 'Status';
TEST $CLI volume start $V2;
EXPECT 'Started' volinfo_field $V2 'Status';

TEST glusterfs -s $H0 --volfile-id=$V0 $M0;
TEST glusterfs -s $H0 --volfile-id=$V1 $M1;
TEST glusterfs -s $H0 --volfile-id=$V2 $M2;

build_tester $(dirname $0)/bug-4850-mmap-oappend.c

## T1: mmap write through an O_APPEND fd lands at the mapped offset
##     and does not change the file size.
TEST $(dirname $0)/bug-4850-mmap-oappend $M0
TEST $(dirname $0)/bug-4850-mmap-oappend $M1
TEST $(dirname $0)/bug-4850-mmap-oappend $M2

## T4: the same test on the local backend must pass, proving the test
##     itself matches local filesystem behavior.
TEST $(dirname $0)/bug-4850-mmap-oappend $B0

## T2a: sequential writes on an O_APPEND fd still append sequentially.
echo "line1" > $M0/append-file
echo "line2" >> $M0/append-file
echo "line3" >> $M0/append-file
EXPECT '3' wc -l < $M0/append-file
EXPECT 'line3' tail -n 1 $M0/append-file

## T2b: explicit-offset writes on non-append fds still honor the offset.
printf ABCDEFGH > $B0/f4850-ref
dd if=$B0/f4850-ref of=$M0/offset-file bs=1 2>/dev/null
printf XY > $B0/f4850-xy
dd if=$B0/f4850-xy of=$M0/offset-file bs=1 seek=2 conv=notrunc,fsync 2>/dev/null
printf ABXYEFGH > $B0/f4850-expect
TEST cmp $B0/f4850-expect $M0/offset-file
EXPECT '8' wc -c < $M0/offset-file

## T3: concurrent appends from multiple processes do not overwrite
##     each other (append must stay atomic at EOF).
TEST rm -f $M0/concurrent-file $M1/concurrent-file
for i in $(seq 1 8); do
        ( for j in $(seq 1 25); do
              echo "line-$i-$j" >> $M0/concurrent-file
              echo "line-$i-$j" >> $M1/concurrent-file
          done ) &
done
wait
TEST dd if=/dev/zero of=$M0/concurrent-file bs=1 count=1 oflag=append conv=notrunc,fsync 2>/dev/null
TEST dd if=/dev/zero of=$M1/concurrent-file bs=1 count=1 oflag=append conv=notrunc,fsync 2>/dev/null
EXPECT '200' wc -l < $M0/concurrent-file
EXPECT '200' wc -l < $M1/concurrent-file

## T5: shard volume: a big O_APPEND write decomposed into chunks keeps
##     its layout intact (the first chunk goes through the original fd).
TEST dd if=/dev/urandom of=$B0/shard-ref bs=1M count=5
TEST touch $M2/shard-file
TEST dd if=$B0/shard-ref of=$M2/shard-file bs=1M oflag=append conv=notrunc,fsync 2>/dev/null
TEST cmp $B0/shard-ref $M2/shard-file

rm -f $(dirname $0)/bug-4850-mmap-oappend
TEST $CLI volume stop $V0
TEST $CLI volume stop $V1
TEST $CLI volume stop $V2

cleanup;
