#!/bin/bash
#
# Regression: dht directory-rename over an existing destination must not inflate
# the source directory inode's fd_count/active_fd_count.
#
# dht_rename_opendir_cbk used to call fd_bind() once per successful per-subvol
# opendir on a single shared fd, leaving the inode's fd_count/active_fd_count
# inflated by (dht_subvols - 1). These counters are the ones an operator reads
# from a statedump when diagnosing fd usage, so the inflation is a correctness
# bug in the diagnostic output. Fixed by binding the fd exactly once.
#
# RED on the buggy dht.so (fd-count = N-1 = 2), GREEN once fd_bind is bound once.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## 3-subvol pure distribute (N=3 -> buggy leaks N-1 = 2)
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
# keep a clean dht-over-client graph so no perf xlator opens/holds extra fds
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.readdir-ahead off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.nl-cache off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
     --attribute-timeout=0 --entry-timeout=0

# --- helpers: read a specific inode's fd-count from the mount statedump ---
# print "present"/"absent" (liveness guard against an empty-grep false pass)
function inode_present {
        local sd=$1 g=$2
        grep -q "gfid=$g" $sd && echo "present" || echo "absent"
}
# print the (max) fd-count reported for an inode gfid
function fdcount_of_gfid {
        local sd=$1 g=$2
        grep -A4 "gfid=$g" $sd | grep 'fd-count=' | cut -d= -f2 | sort -nr | head -1
}

# ============================================================================
# MEASUREMENT POSITIVE CONTROL: prove the statedump fd-count field reads a
# genuine NON-ZERO for a legitimately open dir fd. If this fails the assay is
# broken and the bug assertion below would be vacuous.
# ============================================================================
TEST mkdir $M0/ctl_dir
ctl_gfid=$(getfattr -h -n glusterfs.gfid.string --only-values $M0/ctl_dir 2>/dev/null)
exec 9<$M0/ctl_dir                        # hold a real O_RDONLY dir fd open
sd_ctl=$(generate_mount_statedump $V0)
EXPECT "present" inode_present $sd_ctl $ctl_gfid
EXPECT "1" fdcount_of_gfid $sd_ctl $ctl_gfid
exec 9<&-                                 # release it
rm -f $sd_ctl

# ============================================================================
# THE BUG: rename a directory over an existing (empty) directory.
# ============================================================================
TEST mkdir $M0/src_dir
TEST mkdir $M0/dst_dir
src_gfid=$(getfattr -h -n glusterfs.gfid.string --only-values $M0/src_dir 2>/dev/null)
TEST mv -T $M0/src_dir $M0/dst_dir        # src inode (src_gfid) is now dst_dir
TEST stat $M0/dst_dir                      # keep the inode looked-up/active

sd=$(generate_mount_statedump $V0)
# liveness: the renamed inode must actually be in the dump (else a 0 is a lie)
EXPECT "present" inode_present $sd $src_gfid
# the regression assertion: no phantom fd counts left on the renamed inode
EXPECT "0" fdcount_of_gfid $sd $src_gfid
rm -f $sd

cleanup
