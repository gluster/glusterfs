#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
TEST $CLI volume set $V0 debug.trace marker;
TEST $CLI volume set $V0 debug.log-history on

TEST $CLI volume start $V0;

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 \
$M0;

touch $M0/{1..22};
rm -f $M0/*;

pid_file=$(ls $GLUSTERD_WORKDIR/vols/$V0/run);
brick_pid=$(cat $GLUSTERD_WORKDIR/vols/$V0/run/$pid_file);

mkdir $statedumpdir/statedump_tmp/;
echo "path=$statedumpdir/statedump_tmp" > $statedumpdir/glusterdump.options;
echo "all=yes" >> $statedumpdir/glusterdump.options;

TEST $CLI volume statedump $V0 history;

file_name=$(ls $statedumpdir/statedump_tmp);
TEST grep "xlator.debug.trace.history" $statedumpdir/statedump_tmp/$file_name;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

rm -rf $statedumpdir/statedump_tmp;
rm -f $statedumpdir/glusterdump.options;

cleanup;
