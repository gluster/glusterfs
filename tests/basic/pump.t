#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;
START_TIMESTAMP=`date +%s`

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
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
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 commit force
TEST $CLI volume stop $V0

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

# Check for non Linux systems that we did not mess with directory offsets
TEST ! log_newer $START_TIMESTAMP "offset reused from another DIR"

cleanup
