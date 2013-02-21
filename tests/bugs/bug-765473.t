#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fileio.rc

cleanup;

function clients_connected()
{
    volname=$1
    gluster volume status $volname clients | grep -i 'Clients connected' | sed -e 's/[^0-9]*\(.*\)/\1/g'
}
        
## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 $H0:$B0/${V0}{1}
TEST $CLI volume start $V0;

TEST glusterfs --direct-io-mode=yes --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

TEST fd=`fd_available`
TEST fd_open $fd 'w' "$M0/testfile"
TEST fd_write $fd "content"
TEST $CLI volume stop $V0
# write some content which will result in marking fd bad
fd_write $fd "more content"
TEST $CLI volume start $V0
EXPECT_WITHIN 30 2 clients_connected $V0
TEST ! fd_write $fd "still more content"

cleanup
