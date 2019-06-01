#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume start $V0;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1

TEST stat $M1/.meta/

# expect failures in rpc-coverage.sh execution.
res=$($(dirname $0)/../basic/rpc-coverage.sh $M1/.meta)


# Expect errors here, hence no need to 'check for success'
for file in $(find $M1/.meta type f -print); do
    cat $file >/dev/null
    echo 1>$file
    echo hello>$file
done

TEST umount $M1

cleanup;
