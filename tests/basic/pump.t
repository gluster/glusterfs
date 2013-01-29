#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0
for i in {1..3}
do
        for j in {1..10}
        do
                dd if=/dev/urandom of=file$j bs=128K count=10 2>/dev/null 1>/dev/null
        done
        mkdir dir$i && cd dir$i
done
cd
TEST umount $M0
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 start
EXPECT_WITHIN 60 "Y" gd_is_replace_brick_completed $H0 $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 commit
TEST $CLI volume stop $V0
TEST diff -r --exclude=.glusterfs $B0/${V0}0 $B0/${V0}1

files=""

cd $B0/${V0}0
for f in `find . -path ./.glusterfs -prune  -o -print`;
do
        if [ -d $f ]; then continue; fi
        cmp $f $B0/${V0}1/$f
        if [ $? -ne 0 ]; then
                files="$files $f"
        fi
done

EXPECT "" echo $files

cleanup
