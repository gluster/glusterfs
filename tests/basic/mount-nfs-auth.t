#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

cleanup;
## Check whether glusterd is running
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

# Export variables for allow & deny
EXPORT_ALLOW="/$V0 $H0(sec=sys,rw,anonuid=0) @ngtop(sec=sys,rw,anonuid=0)"
EXPORT_ALLOW_SLASH="/$V0/ $H0(sec=sys,rw,anonuid=0) @ngtop(sec=sys,rw,anonuid=0)"
EXPORT_DENY="/$V0 1.2.3.4(sec=sys,rw,anonuid=0) @ngtop(sec=sys,rw,anonuid=0)"

# Netgroup variables for allow & deny
NETGROUP_ALLOW="ngtop ng1000\nng1000 ng999\nng999 ng1\nng1 ng2\nng2 ($H0,,)"
NETGROUP_DENY="ngtop ng1000\nng1000 ng999\nng999 ng1\nng1 ng2\nng2 (1.2.3.4,,)"

V0L1="$V0/L1"
V0L2="$V0L1/L2"
V0L3="$V0L2/L3"

# Other variations for allow & deny
EXPORT_ALLOW_RO="/$V0 $H0(sec=sys,ro,anonuid=0) @ngtop(sec=sys,ro,anonuid=0)"
EXPORT_ALLOW_L1="/$V0L1 $H0(sec=sys,rw,anonuid=0) @ngtop(sec=sys,rw,anonuid=0)"
EXPORT_WILDCARD="/$V0 *(sec=sys,rw,anonuid=0) @ngtop(sec=sys,rw,anonuid=0)"

function build_dirs () {
        mkdir -p $B0/b{0,1,2}/L1/L2/L3
}

function export_allow_this_host () {
        printf "$EXPORT_ALLOW\n" > /var/lib/glusterd/nfs/exports
}

function export_allow_this_host_with_slash () {
        printf "$EXPORT_ALLOW_SLASH\n" > /var/lib/glusterd/nfs/exports
}

function export_deny_this_host () {
        printf "$EXPORT_DENY\n" > /var/lib/glusterd/nfs/exports
}

function export_allow_this_host_l1 () {
        printf "$EXPORT_ALLOW_L1\n" >> /var/lib/glusterd/nfs/exports
}

function export_allow_wildcard () {
        printf "$EXPORT_WILDCARD\n" >> /var/lib/glusterd/nfs/exports
}

function export_allow_this_host_ro () {
        printf "$EXPORT_ALLOW_RO\n" > /var/lib/glusterd/nfs/exports
}

function netgroup_allow_this_host () {
        printf "$NETGROUP_ALLOW\n" > /var/lib/glusterd/nfs/netgroups
}

function netgroup_deny_this_host () {
        printf "$NETGROUP_DENY\n" > /var/lib/glusterd/nfs/netgroups
}

function create_vol () {
        TEST $CLI vol create $V0 replica 3 $H0:$B0/b0 $H0:$B0/b1 $H0:$B0/b2
}

function setup_cluster() {
        build_dirs                      # Build directories
        export_allow_this_host          # Allow this host in the exports file
        netgroup_allow_this_host        # Allow this host in the netgroups file

        glusterd
        create_vol                      # Create the volume
}

function do_mount () {
        mount_nfs $H0:/$1 $N0 nolock
}

function small_write () {
        dd if=/dev/zero of=$N0/test-small-write count=1 bs=1k 2>&1
}

function bg_write () {
        dd if=/dev/zero of=$N0/test-bg-write count=1 bs=1k &
        BG_WRITE_PID=$!
}

function big_write() {
        dd if=/dev/zero of=$N0/test-big-write count=500 bs=1M
}

function create () {
        touch $N0/create-test
}

function stat_nfs () {
        ls $N0/
}

# Restarts the NFS server
function restart_nfs () {
        PID=$(ps aux | grep nfs | grep sbin/glusterfs | awk '{print $2}')
        CMD=$(ps ax | grep nfs | grep sbin/glusterfs | awk '{$1=$2=$3=$4="";print $0}')
        kill $PID
        $CMD
}

setup_cluster

# run preliminary tests
TEST $CLI vol set $V0 cluster.self-heal-daemon off
TEST $CLI vol set $V0 nfs.disable off
TEST $CLI vol set $V0 cluster.choose-local off
TEST $CLI vol start $V0

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

## NFS server starts with auth disabled
## Do some tests to verify that.

TEST do_mount $V0
TEST umount $N0

## Disallow host
TEST export_deny_this_host
TEST netgroup_deny_this_host
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

## Technically deauthorized this host, but since auth is disabled we should be
## able to do mounts, writes, etc.
TEST do_mount $V0
TEST small_write
TEST umount $N0
TEST do_mount $V0
TEST umount $N0

## Reauthorize this host
export_allow_this_host
netgroup_allow_this_host

## Restart NFS with auth enabled
$CLI vol stop $V0
TEST $CLI vol set $V0 nfs.exports-auth-enable on
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

## Mount NFS
TEST do_mount $V0
## Disallow host
TEST export_deny_this_host
TEST netgroup_deny_this_host

## nfs-server is not enable to load the changes in the export/netgroup files
## dynamically, a restart is required.

$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

## Writes should not be allowed, host is not authorized
TEST ! small_write
## Unmount so we can test mount
TEST umount $N0
## Subsequent ounts should not be allowed, host is not authorized
TEST ! do_mount $V0

## Reauthorize host
TEST export_allow_this_host
TEST netgroup_allow_this_host
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

TEST do_mount $V0
TEST umount $N0

## Allow host in netgroups but not in exports, host should be allowed
TEST export_deny_this_host
TEST netgroup_allow_this_host
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST do_mount $V0
TEST small_write
TEST big_write
TEST umount $N0

## Allow host in exports but not in netgroups, host should be allowed
TEST export_allow_this_host
TEST netgroup_deny_this_host
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST do_mount $V0
TEST umount $N0

## Finally, reauth the host in export and netgroup, test mount & write
TEST export_allow_this_host_l1
TEST netgroup_allow_this_host
$CLI vol stop $V0
$CLI vol start $V0
sleep 2
TEST do_mount $V0L1
TEST small_write

## Failover test: Restarting NFS and then doing a write should pass
bg_write
TEST restart_nfs
TEST wait $BG_WRITE_PID
TEST small_write
TEST umount $N0

## Test deep mounts
TEST do_mount $V0L1
TEST small_write
TEST umount $N0

TEST export_allow_this_host_ro
TEST netgroup_deny_this_host
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST do_mount $V0
TEST ! small_write # Writes should not be allowed
TEST ! create      # Create should not be allowed
TEST stat_nfs      # Stat should be allowed
TEST umount $N0

TEST export_deny_this_host
TEST netgroup_deny_this_host
TEST export_allow_this_host_l1 # Allow this host at L1
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST ! do_mount $V0 #V0 shouldnt be allowed
TEST do_mount $V0L1 #V0L1 should be
TEST umount $N0

## Test wildcard hosts
TEST export_allow_wildcard
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST do_mount $V0
TEST small_write
TEST umount $N0

## Test if path is parsed correctly
## by mounting host:vol/ instead of host:vol
TEST do_mount $V0/
TEST small_write
TEST umount $N0

TEST export_allow_this_host_with_slash
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

TEST do_mount $V0
TEST small_write
TEST umount $N0
TEST do_mount $V0/
TEST small_write
TEST umount $N0


## Turn off exports authentication
$CLI vol stop $V0
TEST $CLI vol set $V0 nfs.exports-auth-enable off
$CLI vol start $V0

TEST export_deny_this_host # Deny the host
TEST netgroup_deny_this_host
$CLI vol stop $V0
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

TEST do_mount $V0 # Do a mount & test
TEST umount $N0

## Turn back on the exports authentication
$CLI vol stop $V0
TEST $CLI vol set $V0 nfs.exports-auth-enable on
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
## Do a simple test to set the refresh time to 20 seconds
TEST $CLI vol set $V0 nfs.auth-refresh-interval-sec 20

## Do a simple test to see if the volume option exists
TEST $CLI vol set $V0 nfs.auth-cache-ttl-sec 400

## Finish up
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup
