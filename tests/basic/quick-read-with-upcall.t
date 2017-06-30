#!/bin/bash

. $(dirname $0)/../include.rc
 #. $(dirname $0)/../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE without selinux:
TEST glusterfs -s $H0 --volfile-id $V0 $M0;
TEST glusterfs -s $H0 --volfile-id $V0 $M1;

D0="test-message0";
D1="test-message1";

function write_to()
{
    local file="$1";
    local data="$2";
    echo "$data" > "$file";
}


TEST write_to "$M0/test.txt" "$D0"
EXPECT "$D0" cat $M0/test.txt
EXPECT "$D0" cat $M1/test.txt

TEST write_to "$M0/test.txt" "$D1"
EXPECT "$D1" cat $M0/test.txt
EXPECT "$D0" cat $M1/test.txt

sleep 1
EXPECT "$D1" cat $M1/test.txt

TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 performance.qr-cache-timeout 60
TEST $CLI volume set $V0 performance.md-cache-timeout 60

TEST write_to "$M0/test1.txt" "$D0"
EXPECT "$D0" cat $M0/test1.txt
EXPECT "$D0" cat $M1/test1.txt

TEST write_to "$M0/test1.txt" "$D1"
EXPECT "$D1" cat $M0/test1.txt
EXPECT "$D0" cat $M1/test1.txt

sleep 1
EXPECT "$D0" cat $M1/test1.txt

sleep 60
EXPECT "$D1" cat $M1/test1.txt

TEST $CLI volume set $V0 performance.cache-invalidation on

TEST write_to "$M0/test2.txt" "$D0"
EXPECT "$D0" cat $M0/test2.txt
EXPECT "$D0" cat $M1/test2.txt

TEST write_to "$M0/test2.txt" "$D1"
EXPECT "$D1" cat $M0/test2.txt
EXPECT "$D1" cat $M1/test2.txt
