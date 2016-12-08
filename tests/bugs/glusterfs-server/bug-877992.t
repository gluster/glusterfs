#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;


## Start and create a volume
TEST glusterd -LDEBUG
TEST pidof glusterd


function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


function hooks_prep ()
{
    local event=$1
    touch /tmp/pre.out /tmp/post.out
    touch $GLUSTERD_WORKDIR/hooks/1/"$event"/pre/Spre.sh
    touch $GLUSTERD_WORKDIR/hooks/1/"$event"/post/Spost.sh

    printf "#! /bin/bash\necho "$event"Pre > /tmp/pre.out\n" > $GLUSTERD_WORKDIR/hooks/1/"$event"/pre/Spre.sh
    printf "#! /bin/bash\necho "$event"Post > /tmp/post.out\n" > $GLUSTERD_WORKDIR/hooks/1/"$event"/post/Spost.sh
    chmod a+x $GLUSTERD_WORKDIR/hooks/1/"$event"/pre/Spre.sh
    chmod a+x $GLUSTERD_WORKDIR/hooks/1/"$event"/post/Spost.sh
}

function hooks_cleanup ()
{
    local event=$1
    rm /tmp/pre.out /tmp/post.out
    rm $GLUSTERD_WORKDIR/hooks/1/"$event"/pre/Spre.sh
    rm $GLUSTERD_WORKDIR/hooks/1/"$event"/post/Spost.sh
}

## Verify volume is created and its hooks script ran
hooks_prep 'create'
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'createPre' cat /tmp/pre.out;
EXPECT 'createPost' cat /tmp/post.out;
hooks_cleanup 'create'


## Start volume and verify that its hooks script ran
hooks_prep 'start'
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN 5 'startPre' cat /tmp/pre.out;
EXPECT_WITHIN 5 'startPost' cat /tmp/post.out;
hooks_cleanup 'start'

cleanup;
