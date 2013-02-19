#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

TEST $CLI volume start $V0;

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST glusterfs -s $H0 --volfile-id $V0 $M1;

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

# open-behind delays open and uses anonymous fds for fops like
# fstat and readv. So after creating the file, if volume is restarted
# then later when the file is read, because of the use of anonymous fds
# volume top open will show number of files opened as 0.
TEST $CLI volume stop $V0;
sleep 1;
TEST $CLI volume start $V0;

sleep 2;
cat $M1/$F0 >/dev/null;

string=$(gluster volume top $V0 open | grep -w "$F0");

EXPECT "" echo $string;

TEST $CLI volume set $V0 performance.open-behind off;

D1="hello-this-is-a-test-message1";
F1="test-file1";

TEST write_to "$M0/$F1" "$D1";
EXPECT "$D1" cat $M1/$F1;

EXPECT "$D0" cat $M1/$F0;

gluster volume top $V0 open | grep -w "$F0" >/dev/null 2>&1
TEST [ $? -eq 0 ];

cleanup;
