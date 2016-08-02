#!/bin/bash

. $(dirname $0)/../../include.rc
#. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1};
TEST $CLI volume start $V0

# Mount FUSE without selinux:
TEST glusterfs -s $H0 --volfile-id $V0 $@ $M0

#Get the client log file
log_wd=$(gluster --print-logdir)
log_id=${M0:1}     # Remove initial slash
log_id=${log_id//\//-} # Replace remaining slashes with dashes
log_file=$log_wd/$log_id.log

#Set the client xlator log-level to TRACE and check if the TRACE logs get
#printed
TEST setfattr -n trusted.glusterfs.$V0-client-0.set-log-level -v TRACE $M0
TEST ! stat $M0/xyz
grep -q " T \[rpc-clnt\.c" $log_file
res=$?
EXPECT '0' echo $res

#Set the client xlator log-level to INFO and make sure the TRACE logs do
#not get printed
echo > $log_file
TEST setfattr -n trusted.glusterfs.$V0-client-0.set-log-level -v INFO $M0
TEST ! stat $M0/xyz
grep -q " T \[rpc-clnt\.c" $log_file
res=$?
EXPECT_NOT '0' echo $res

cleanup;
