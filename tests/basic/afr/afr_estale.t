#!/bin/bash
#Test for estale when fs is stale 

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

xargs_stat(){
        find $M0/file | xargs stat
}

TEST glusterd;
TEST $CLI volume info;
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST /usr/local/sbin/glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

cd $M0
TEST touch file
TEST setfattr -n trusted.gfid -v 0sBfz5vAdHTEK1GZ99qjqTIg== /d/backends/patchy0/file
TEST ! xargs_stat

cleanup;

