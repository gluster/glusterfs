#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

# This is currently static 5 brick (2MDS 3DS configuration)
brick_list=$(echo $H0:$B0/${V0}{1..5})

# The helper script creates the volume as well, at present
$PYTHON $(dirname $0)/../../xlators/experimental/rio/scripts/rio-volfile-generator/GlusterCreateVolume.py $V0 2 3 "$brick_list"

EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

cd /var/lib/glusterd/vols/$V0
for i in $(ls *.gen); do j=$(echo $i | sed 's/\(.gen\)//'); mv -f $i $j; done
cd -

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M0

cleanup;
