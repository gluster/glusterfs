#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

#This script checks if use-readdirp option works as accepted in mount options


TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST mkdir $M0/nobody
TEST chown nfsnobody:nfsnobody $M0/nobody
TEST `echo "file" >> $M0/file`
TEST cp $M0/file $M0/new
TEST chmod 700 $M0/new
TEST cat $M0/new

TEST $CLI volume set $V0 server.root-squash enable
TEST `echo 3 > /proc/sys/vm/drop_caches`
TEST ! mkdir $M0/other
TEST mkdir $M0/nobody/other
TEST cat $M0/file
TEST ! cat $M0/new
TEST `echo "nobody" >> $M0/nobody/file`

#mount the client without root-squashing
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 --no-root-squash=yes $M1
TEST mkdir $M1/m1_dir
TEST `echo "file" >> $M1/m1_file`
TEST cp $M0/file $M1/new
TEST chmod 700 $M1/new
TEST cat $M1/new

TEST $CLI volume set $V0 server.root-squash disable
TEST mkdir $M0/other
TEST cat $M0/new

cleanup
