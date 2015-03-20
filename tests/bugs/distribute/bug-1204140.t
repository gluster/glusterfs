#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc

cleanup;


TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;
TEST touch $M0/new.txt;
TEST getfattr -n "glusterfs.get_real_filename:NEW.txt" $M0;
TEST ! getfattr -n "glusterfs.get_realfilename:NEXT.txt" $M0;


cleanup;
