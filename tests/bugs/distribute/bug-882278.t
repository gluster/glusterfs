#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup

# Is there a good reason to require --fqdn elsewhere?  It's worse than useless
# here.
H0=$(hostname -s)

function recreate {
	# The rm is necessary so we don't get fooled by leftovers from old runs.
	rm -rf $1 && mkdir -p $1
}

function count_lines {
	grep "$1" $2/* | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
TEST recreate ${B0}/${V0}-0
TEST recreate ${B0}/${V0}-1
TEST $CLI volume create $V0 $H0:$B0/${V0}-{0,1}
TEST $CLI volume set $V0 cluster.nufa on

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount native
special_option="--xlator-option ${V0}-dht.local-volume-name=${V0}-client-1"
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $special_option $M0

## Create a bunch of test files.
for i in $(seq 0 99); do
	echo hello > $(printf $M0/file%02d $i)
done

## Make sure the files went to the right place.  There might be link files in
## the other brick, but they won't have any contents.
EXPECT "0" count_lines hello ${B0}/${V0}-0
EXPECT "100" count_lines hello ${B0}/${V0}-1

if [ "$EXIT_EARLY" = "1" ]; then
	exit 0;
fi

## Finish up
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
