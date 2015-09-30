#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

case $OSTYPE in
NetBSD)
        echo "Skip test on ACL which are not available on NetBSD" >&2
        SKIP_TESTS
        exit 0
        ;;
*)
        ;;
esac

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4};
TEST $CLI volume start $V0;

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 --acl -s $H0 --volfile-id $V0 $M0;

TEST touch $M0/file1;

TEST $CC $(dirname $0)/bug-1051896.c -o $(dirname $0)/bug-1051896 -lacl
TEST ! $(dirname $0)/bug-1051896 $M0/file1 m 'u::r,u::w,g::r--,o::r--'
TEST rm -f $(dirname $0)/bug-1051896

cleanup
