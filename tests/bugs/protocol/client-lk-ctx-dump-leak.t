#!/bin/bash
#
# protocol/client's statedump writer, client_fd_lk_ctx_dump(), takes a
# reference on the lock context of every fd the client has open, and it is
# also what a read of .meta/graphs/active/<client>/private prints. It dropped
# that reference only for an fd that held locks: for any other fd the
# fd_lk_ctx_t outlived the fd, one per fd that was open during a dump.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

N=5

# Live fd lock contexts: fd_create() gives every fd_t one fd_lk_ctx_t,
# accounted to the fuse xlator. The section is only printed when the count
# is non-zero. A statedump is itself a dump of the client, so this is only
# called while no file is open.
function fuse_lk_ctx_count {
        local sd=$(generate_mount_statedump $V0 $M0)
        awk -F= '/^\[mount\/fuse\.fuse - usage-type gf_common_mt_fd_lk_ctx_t memusage\]$/ {f=1}
                 f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$sd"
        rm -f "$sd"
}

# fds the client has open on the brick, as its private dump lists them
function client_saved_fd_count {
        grep -c '^fd\.[0-9]*\.remote_fd = ' $M0/.meta/graphs/active/$V0-client-0/private
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
# open-behind would keep the opens away from protocol/client
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume start $V0

# create the files through another mount, so that $M0 never had a file open
# before the dumps below
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1
for i in $(seq 1 $N); do
        echo data > $M1/f$i
done
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT "0" fuse_lk_ctx_count

# open the files without taking any lock, and dump the client while they are
# open: twice, through .meta (each read is a full client_priv_dump())
for i in $(seq 1 $N); do
        eval "exec $((10 + i))<>$M0/f$i"
done
EXPECT "$N" client_saved_fd_count
EXPECT "$N" client_saved_fd_count

for i in $(seq 1 $N); do
        eval "exec $((10 + i))>&-"
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" fuse_lk_ctx_count

cleanup;
