#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
TEST $CLI volume set $V0 debug.trace marker;
TEST $CLI volume set $V0 debug.log-history on

TEST $CLI volume start $V0;

sleep 1;
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 \
$M0;

sleep 5;

touch $M0/{1..22};
rm -f $M0/*;

pid_file=$(ls /var/lib/glusterd/vols/$V0/run);
brick_pid=$(cat /var/lib/glusterd/vols/$V0/run/$pid_file);

mkdir $statedumpdir/statedump_tmp/;
echo "path=$statedumpdir/statedump_tmp" > $statedumpdir/glusterdump.options;
echo "all=yes" >> $statedumpdir/glusterdump.options;

TEST $CLI volume statedump $V0 history;

file_name=$(ls $statedumpdir/statedump_tmp);
TEST grep "xlator.debug.trace.history" $statedumpdir/statedump_tmp/$file_name;

TEST umount $M0

rm -rf $statedumpdir/statedump_tmp;
rm -f $statedumpdir/glusterdump.options;

cleanup;
