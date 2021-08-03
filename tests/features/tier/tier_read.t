#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# (9+9) -1 (-1 for rename)
TESTS_EXPECTED_IN_LOOP=17

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

cold_dir=$(mktemp -d -u -p ${PWD})
TEST mkdir -p ${cold_dir};
#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};
TEST $CLI volume create $V0 $H0:$B0/${V0}

TEST $CLI volume set $V0 tier-storetype filesystem;
#TEST $CLI volume set $V0 quick-read off;
#TEST $CLI volume set $V0 stat-prefetch off;
TEST $CLI volume start $V0;

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};


#TEST $CLI volume stop $V0;
#TEST $CLI volume start $V0 force;

EXPECT 'Started' volinfo_field $V0 'Status';

sleep 1

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

declare -A  before
declare -A  mtime
for i in $(seq 1 10); do
    echo $i > $M1/testfile$i;
done

cp -R /etc $M1

# Just try running the command on local file
setfattr -n tier.promote-file-as-hot -v true $M1/etc/hosts

/usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 $cold_dir $B0/${V0} etc/kernel-img.conf
# /usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 $cold_dir $B0/${V0}2 etc/kernel-img.conf
# /usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 $cold_dir $B0/${V0}3 etc/kernel-img.conf
# /usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 $cold_dir $B0/${V0}4 etc/kernel-img.conf
# /usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 $cold_dir $B0/${V0}5 etc/kernel-img.conf

sleep .001;

cat $M1/etc/kernel-img.conf

setfattr -n tier.promote-file-as-hot -v true $M1/etc/kernel-img.conf

#umount $M1;

#TEST $GFS -s $H0 --volfile-id $V0 $M1;

for i in $(seq 1 10); do
    before[$i]=$(md5sum $M1/testfile$i | awk '{ print $1 }');
    mtime[$i]=`stat -c "%.Y" $M1/testfile$i`
done

sleep 1;

TEST cp -a $M1/testfile* ${cold_dir}/

#post processing of upload
for i in $(seq 1 10); do
    mt=${mtime[$i]}
    TEST setfattr -n tier.mark-file-as-remote -v $mt $M1/testfile$i;
done

sleep 1;

#umount $M1;

#TEST $GFS -s $H0 --volfile-id $V0 $M1;

#sleep 1;

for i in $(seq 1 9); do
    after=$(md5sum $M1/testfile$i | awk '{ print $1 }');
    b=${before[$i]}
    TEST [ "$after" = "$b" ]
done

TEST mv $M1/testfile10 $M1/new-moved-file

after=$(md5sum $M1/new-moved-file | awk '{ print $1 }');
b=${before[10]}
TEST [ "$after" = "$b" ]

getfattr -e text -n tier.remote-read-count $M1/testfile1

TEST rm -rf $cold_dir;

cleanup;
