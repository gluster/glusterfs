#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

# Our mount timeout must be as long as the time for a regular configuration
# change to be acted upon *plus* AUTH_REFRESH_TIMEOUT, not one replacing the
# other.  Otherwise this process races vs. the one making the change we're
# trying to test, which leads to spurious failures.
MY_MOUNT_TIMEOUT=$((CONFIG_UPDATE_TIMEOUT+AUTH_REFRESH_INTERVAL))

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
        printf "$EXPORT_ALLOW\n" > ${NFSDIR}/exports
}

function export_allow_this_host_with_slash () {
        printf "$EXPORT_ALLOW_SLASH\n" > ${NFSDIR}/exports
}

function export_deny_this_host () {
        printf "$EXPORT_DENY\n" > ${NFSDIR}/exports
}

function export_allow_this_host_l1 () {
        printf "$EXPORT_ALLOW_L1\n" >> ${NFSDIR}/exports
}

function export_allow_wildcard () {
        printf "$EXPORT_WILDCARD\n" > ${NFSDIR}/exports
}

function export_allow_this_host_ro () {
        printf "$EXPORT_ALLOW_RO\n" > ${NFSDIR}/exports
}

function netgroup_allow_this_host () {
        printf "$NETGROUP_ALLOW\n" > ${NFSDIR}/netgroups
}

function netgroup_deny_this_host () {
        printf "$NETGROUP_DENY\n" > ${NFSDIR}/netgroups
}

function create_vol () {
        $CLI vol create $V0 $H0:$B0/b0
}

function setup_cluster() {
        build_dirs                      # Build directories
        export_allow_this_host          # Allow this host in the exports file
        netgroup_allow_this_host        # Allow this host in the netgroups file

        glusterd
        create_vol                      # Create the volume
}

function check_mount_success {
        mount_nfs $H0:/$1 $N0 nolock
        if [ $? -eq 0 ]; then
                echo "Y"
        else
                echo "N"
        fi
}

function check_mount_failure {
        mount_nfs $H0:/$1 $N0 nolock
        if [ $? -ne 0 ]; then
                echo "Y"
        else
                local timeout=$UMOUNT_TIMEOUT
                while ! umount_nfs $N0 && [$timeout -ne 0] ; do
                        timeout=$(( $timeout - 1 ))
                        sleep 1
                done
        fi
}

function small_write () {
        dd if=/dev/zero of=$N0/test-small-write count=1 bs=1k 2>&1
        if [ $? -ne 0 ]; then
                echo "N"
        else
                echo "Y"
        fi
}

function bg_write () {
        dd if=/dev/zero of=$N0/test-bg-write count=1 bs=1k &
        BG_WRITE_PID=$!
}

function big_write() {
        dd if=/dev/zero of=$N0/test-big-write count=500 bs=1024k
}

function create () {
        touch $N0/create-test
}

function stat_nfs () {
        ls $N0/
}

# Restarts the NFS server
function restart_nfs () {
        local NFS_PID=$(cat ${GLUSTERD_WORKDIR}/nfs/run/nfs.pid)

        # kill the NFS-server if it is running
        while ps -q ${NFS_PID} 2>&1 > /dev/null; do
                kill ${NFS_PID}
                sleep 0.5
        done

        # start-force starts the NFS-server again
        $CLI vol start patchy force
}

setup_cluster

# run preliminary tests
TEST $CLI vol set $V0 nfs.disable off
TEST $CLI vol start $V0

# Get NFS state directory
NFSDIR=$( $CLI volume get patchy nfs.mount-rmtab | \
          awk '/^nfs.mount-rmtab/{print $2}' | \
          xargs dirname )

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

## NFS server starts with auth disabled
## Do some tests to verify that.

EXPECT "Y" check_mount_success $V0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Disallow host
TEST export_deny_this_host
TEST netgroup_deny_this_host

## Technically deauthorized this host, but since auth is disabled we should be
## able to do mounts, writes, etc.
EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Reauthorize this host
export_allow_this_host
netgroup_allow_this_host

## Restart NFS with auth enabled
$CLI vol stop $V0
TEST $CLI vol set $V0 nfs.exports-auth-enable on
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

## Mount NFS
EXPECT "Y" check_mount_success $V0

## Disallow host
TEST export_deny_this_host
TEST netgroup_deny_this_host

## Writes should not be allowed, host is not authorized
EXPECT_WITHIN $AUTH_REFRESH_INTERVAL "N" small_write

## Unmount so we can test mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Subsequent ounts should not be allowed, host is not authorized
EXPECT "Y" check_mount_failure $V0

## Reauthorize host
TEST export_allow_this_host
TEST netgroup_allow_this_host

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Allow host in netgroups but not in exports, host should be allowed
TEST export_deny_this_host
TEST netgroup_allow_this_host

# wait for the mount authentication to rebuild
sleep $[$AUTH_REFRESH_INTERVAL + 1]

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT "Y" small_write
TEST big_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Allow host in exports but not in netgroups, host should be allowed
TEST export_allow_this_host
TEST netgroup_deny_this_host

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Finally, reauth the host in export and netgroup, test mount & write
TEST export_allow_this_host_l1
TEST netgroup_allow_this_host

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0L1
EXPECT "Y" small_write

## Failover test: Restarting NFS and then doing a write should pass
bg_write
TEST restart_nfs
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

TEST wait $BG_WRITE_PID
EXPECT "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Test deep mounts
EXPECT "Y" check_mount_success $V0L1
EXPECT "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST export_allow_this_host_ro
TEST netgroup_deny_this_host

## Restart the nfs server to avoid spurious failure(BZ1256352)
restart_nfs
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT "N" small_write # Writes should not be allowed
TEST ! create      # Create should not be allowed
TEST stat_nfs      # Stat should be allowed
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST export_deny_this_host
TEST netgroup_deny_this_host
TEST export_allow_this_host_l1 # Allow this host at L1

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_failure $V0 #V0 shouldnt be allowed
EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0L1 #V0L1 should be
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Test wildcard hosts
TEST export_allow_wildcard

# the $MY_MOUNT_TIMEOUT might not be long enough? restart should do
restart_nfs
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT_WITHIN $AUTH_REFRESH_INTERVAL "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Test if path is parsed correctly
## by mounting host:vol/ instead of host:vol
EXPECT "Y" check_mount_success $V0/
EXPECT "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST export_allow_this_host_with_slash

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0
EXPECT "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

EXPECT "Y" check_mount_success $V0/
EXPECT "Y" small_write
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0


## Turn off exports authentication
$CLI vol stop $V0
TEST $CLI vol set $V0 nfs.exports-auth-enable off
$CLI vol start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

TEST export_deny_this_host # Deny the host
TEST netgroup_deny_this_host

EXPECT_WITHIN $MY_MOUNT_TIMEOUT "Y" check_mount_success $V0 # Do a mount & test
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

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
