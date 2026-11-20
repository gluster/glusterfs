#!/bin/bash

# fallocate, discard (punch hole) and zerofill change file data without
# a writev. bit-rot-stub has to bump the object version for them, or the
# signer never re-signs the object and the scrubber flags it as corrupted.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

SIGN_TIMEOUT=180

function get_version {
        get_hex_xattr trusted.bit-rot.version $1
}

# Y once the signature was made for the current object version.
function signed_current {
        local sig=$(get_hex_xattr trusted.bit-rot.signature $1)
        local ver=$(get_version $1)
        if [ -n "$sig" ] && [ "${sig:2:16}" == "${ver:0:16}" ]; then
                echo "Y"
        else
                echo "N"
        fi
}

function version_changed {
        if [ "$(get_version $1)" != "$2" ]; then echo "Y"; else echo "N"; fi
}

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume start $V0

TEST $CLI volume bitrot $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Active' scrub_status $V0 'State of scrub'
TEST $CLI volume set $V0 features.expiry-time 1

TEST build_tester $(dirname $0)/br-zerofill.c -lgfapi -Wall -O2

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

# Signed objects: the first data change on each one goes through
# object versioning (inode is dirty after signing).
TEST dd if=/dev/urandom of=$M0/PUNCH bs=64k count=16
TEST dd if=/dev/urandom of=$M0/ZERO bs=64k count=16
TEST dd if=/dev/urandom of=$M0/ALLOC bs=64k count=16
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/PUNCH
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/ZERO
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/ALLOC

V_PUNCH=$(get_version $B0/${V0}1/PUNCH)
V_ZERO=$(get_version $B0/${V0}1/ZERO)
V_ALLOC=$(get_version $B0/${V0}1/ALLOC)

TEST fallocate -p -o 65536 -l 65536 $M0/PUNCH
TEST $(dirname $0)/br-zerofill $H0 $V0 /ZERO 131072 65536 `gluster --print-logdir`/glfs-$V0.log
TEST fallocate -o 1048576 -l 1048576 $M0/ALLOC

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" version_changed $B0/${V0}1/PUNCH $V_PUNCH
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" version_changed $B0/${V0}1/ZERO $V_ZERO
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" version_changed $B0/${V0}1/ALLOC $V_ALLOC

# New, never written object: fallocate has to mark it modified so that
# the signer is told about it on release.
TEST touch $M0/NEW
TEST fallocate -l 1048576 $M0/NEW

# Let the signer pick up the new versions, then scrub.
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/PUNCH
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/ZERO
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/ALLOC
EXPECT_WITHIN $SIGN_TIMEOUT "Y" signed_current $B0/${V0}1/NEW

TEST $CLI volume bitrot $V0 scrub ondemand
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '^4$' scrub_status $V0 'Number of Scrubbed files'
EXPECT '^0$' scrub_status $V0 'Error count'

TEST ! getfattr -n 'trusted.bit-rot.bad-file' $B0/${V0}1/PUNCH
TEST ! getfattr -n 'trusted.bit-rot.bad-file' $B0/${V0}1/ZERO
TEST ! getfattr -n 'trusted.bit-rot.bad-file' $B0/${V0}1/ALLOC
TEST ! getfattr -n 'trusted.bit-rot.bad-file' $B0/${V0}1/NEW

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST rm -f $(dirname $0)/br-zerofill
cleanup;
