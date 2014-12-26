#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Start and create a volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1}

TEST $CLI volume set $V0 performance.io-cache off;
TEST $CLI volume set $V0 performance.write-behind off;
TEST $CLI volume set $V0 performance.stat-prefetch off

TEST $CLI volume start $V0;

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

#returns success if 'olddir' is absent
#'olddir' must be absent in both replicas
function rm_succeeded () {
    local dir1=$1
    [[ -d $H0:$B0/${V0}-0/$dir1 || -d $H0:$B0/${V0}-1/$dir1 ]] && return 0
    return 1
}

# returns successes if 'newdir' is present
#'newdir' must be present in both replicas
function mv_succeeded () {
    local dir1=$1
    [[ -d $H0:$B0/${V0}-0/$dir1 && -d $H0:$B0/${V0}-1/$dir1 ]] && return 1
    return 0
}

# returns zero on success
# Only one of rm and mv can succeed. This is captured by the XOR below

function chk_backend_consistency(){
    local dir1=$1
    local dir2=$2
    local rm_status=rm_succeeded $dir1
    local mv_status=mv_succeeded $dir2
    [[ ( $rm_status && ! $mv_status ) || ( ! $rm_status && $mv_status ) ]] && return 0
    return 1
}

#concurrent removal/rename of dirs
function rm_mv_correctness () {
    ret=0
    for i in {1..100}; do
        mkdir $M0/"dir"$i
        rmdir $M0/"dir"$i &
        mv $M0/"dir"$i $M0/"adir"$i &
        wait
        tmp_ret=$(chk_backend_consistency "dir"$i "adir"$i)
        (( ret += tmp_ret ))
        rm -rf $M0/"dir"$i
        rm -rf $M0/"adir"$i
    done
    return $ret
}

TEST touch $M0/a;
TEST mv $M0/a $M0/b;

#test rename fop when one of the bricks is down
kill_brick ${V0} ${H0} ${B0}/${V0}-1;
TEST touch $M0/h;
TEST mv $M0/h $M0/1;

TEST $CLI volume start $V0 force;

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1;
find $M0 2>/dev/null 1>/dev/null;
find $M0 | xargs stat 2>/dev/null 1>/dev/null;

TEST rm_mv_correctness;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup;

