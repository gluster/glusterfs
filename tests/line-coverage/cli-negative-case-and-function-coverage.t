#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

# Start glusterd
TEST glusterd;
TEST pidof glusterd;

# Checking various negative cases of gluster cli
# Wrong brick type
TEST ! $CLI volume create ${V0}_1 $H0-$B0/v{1..2}

# Wrong hostname
TEST ! $CLI volume create ${V0}_1 $S0:$B0/v{1..2}

# 'localhost' as hostname not allowed
TEST ! $CLI volume create ${V0}_1 localhost:$B0/v{1..2}

# inode-quota option
# volume name not provided
TEST ! $CLI volume inode-quota disable

# only enabling is allowed
TEST ! $CLI volume inode-quota ${V0}_1 disable

# quota options
# provide wrong value
TEST ! $CLI volume ${V0}_1 start
TEST ! $CLI volume ${V0}_1 limit-usage /random-path 0
TEST ! $CLI volume ${V0}_1 limit-objects /random-path 0
TEST ! $CLI volume ${V0}_1 alert-time some-time
TEST ! $CLI volume ${V0}_1 soft-timeout some-time
TEST ! $CLI volume ${V0}_1 hard-timeout some-time

# absolute path not given
TEST ! $CLI volume ${V0}_1 limit-usage random-path
TEST ! $CLI volume ${V0}_1 remove random-path
TEST ! $CLI volume ${V0}_1 remove-objects random-path
TEST ! $CLI volume ${V0}_1 list random-path

# value not provided
TEST ! $CLI volume ${V0}_1 remove /random-path
TEST ! $CLI volume ${V0}_1 remove-objects /random-path
TEST ! $CLI volume ${V0}_1 alert-time
TEST ! $CLI volume ${V0}_1 soft-timeout
TEST ! $CLI volume ${V0}_1 hard-timeout
TEST ! $CLI volume ${V0}_1 default-soft-limit

# nfs-ganesha options
TEST ! $CLI nfs-ganesha
TEST ! $CLI nfs-gansha disable
TEST ! $CLI nfs-ganesha stop
TEST ! $CLI nfs-ganesha disable
TEST ! $CLI nfs-ganesha enable

# peer options
TEST ! $CLI peer probe
TEST ! $CLI peer probe host_name
TEST ! $CLI peer detach
TEST ! $CLI peer detach host-name random-option
TEST ! $CLI peer status host
TEST ! $CLI pool list host

# vol sync option
TEST ! $CLI vol sync
TEST ! $CLI vol sync host-name
TEST ! $CLI vol sync localhost

# system commands
TEST ! $CLI system:: getspec
TEST ! $CLI system:: portmap brick2port
TEST ! $CLI system:: fsm log random-peer random-value
TEST ! $CLI system:: getwd random-value
TEST ! $CLI system:: mount
TEST ! $CLI system:: umount
TEST ! $CLI system:: uuid get random-value
TEST ! $CLI system:: uuid reset random-value
TEST ! $CLI system:: execute
TEST ! $CLI system:: copy file

# volume status statistics
TEST $CLI volume create ${V0}_1 replica 3 $H0:$B0/v{1..3}
TEST $CLI volume start ${V0}_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST $CLI volume heal ${V0}_1 statistics

# xml options
TEST $CLI volume replace-brick ${V0}_1 $H0:$B0/v1 $H0:$B0/v4 commit force --xml
TEST $CLI volume create ${V0}_2 $H0:$B0/v{5..6} --xml
TEST $CLI volume delete ${V0}_2 --xml

# volume commands
TEST ! $CLI volume start
TEST ! $CLI volume start ${V0}_1 frc
TEST ! $CLI volume info ${V0}_1 info
TEST ! $CLI volume info ${V0}_2
TEST ! $CLI volume delete
TEST ! $CLI volume stop
TEST ! $CLI volume stop ${V0}_1 frc
TEST ! $CLI volume rebalance ${V0}_1
TEST ! $CLI volume reset
TEST ! $CLI volume profile ${V0}_1
TEST ! $CLI volume quota all
TEST ! $CLI volume reset-brick ${V0}_1
TEST ! $CLI volume top ${V0}_1
TEST ! $CLI volume log rotate
TEST ! $CLI volume status all all
TEST ! $CLI volume heal
TEST ! $CLI volume statedump
TEST ! $CLI volume clear-locks ${V0}_1 / kid granted entry dir1
TEST ! $CLI volume clear-locks ${V0}_1 / kind grant entry dir1
TEST ! $CLI volume clear-locks ${V0}_1 / kind granted ent dir1
TEST ! $CLI volume barrier ${V0}_1

cleanup;
