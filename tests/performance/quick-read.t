#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

TEST $CLI volume start $V0;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M0;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

D0="hello-this-is-a-test-message0";
F0="test-file0";

function write_to()
{
    local file="$1";
    local data="$2";

    echo "$data" > "$file";
}


TEST write_to "$M0/$F0" "$D0";
EXPECT "$D0" cat $M1/$F0;
EXPECT "$D0" cat $M0/$F0;
EXPECT "$D0" cat $M1/$F0;
EXPECT "$D0" cat $M0/$F0;

sleep 1;

EXPECT "$D0" cat $M1/$F0;
EXPECT "$D0" cat $M0/$F0;
EXPECT "$D0" cat $M1/$F0;
EXPECT "$D0" cat $M0/$F0;

TEST $CLI volume set $V0 performance.quick-read off;

D1="hello-this-is-a-test-message1";
F1="test-file1";

TEST write_to "$M0/$F1" "$D1";
EXPECT "$D1" cat $M0/$F1;

EXPECT "$D0" cat $M1/$F0;

cleanup;
