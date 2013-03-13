#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

# sleeps till the portmap registration of nfs services is done
# NOTE: may take some time for nfs to export all volumes, hence,
# showmount -e succeeding does NOT mean all volumes are available
# for mount. In this case, its a single single-brick volume,
# so no problem.
sleep_till_nfs_awakens ()
{
    while true
    do
        showmount -e 0 > /dev/null 2>&1;
        if [ $? -eq 0 ]; then
            return;
        else
            sleep 1;
        fi
    done
}

#1
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
sleep_till_nfs_awakens
TEST mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0 $N0
TEST umount $N0

# based on ip addresses (1-4)
# case 1: allow only localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-allow 127.0.0.1
sleep_till_nfs_awakens

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 2: allow only non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-allow 192.168.1.1
sleep_till_nfs_awakens
#11
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST $CLI volume reset --mode=script $V0
# case 3: reject only localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-reject 127.0.0.1
sleep_till_nfs_awakens

TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 4: reject only non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.168.1.1
sleep_till_nfs_awakens

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0



# NEED TO CHECK BOTH IP AND NAME BASED AUTH.
# CASES WITH NFS.ADDR-NAMELOOKUP ON (5-12)
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
sleep_till_nfs_awakens
#20
TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 5: allow only localhost
TEST $CLI volume set $V0 nfs.rpc-auth-allow localhost
sleep_till_nfs_awakens

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 6: allow only somehost
TEST $CLI volume set $V0 nfs.rpc-auth-allow somehost
sleep_till_nfs_awakens

TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 7: reject only localhost
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-reject localhost
sleep_till_nfs_awakens
#30
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 8: reject only somehost
TEST $CLI volume set $V0 nfs.rpc-auth-reject somehost
sleep_till_nfs_awakens

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# based on ip addresses: repeat of cases 1-4
# case 9: allow only localhost ip
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-allow 127.0.0.1
sleep_till_nfs_awakens

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 10: allow a non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-allow 192.168.1.1
sleep_till_nfs_awakens
#40
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 11: reject only localhost ip
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-reject 127.0.0.1
sleep_till_nfs_awakens

TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 12: reject only non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.168.1.1
sleep_till_nfs_awakens

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

TEST $CLI volume stop --mode=script $V0
#49
TEST $CLI volume delete --mode=script $V0
cleanup
