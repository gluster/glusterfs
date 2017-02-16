#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

##  Start glusterd
TEST glusterd;

##  Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

##  Enable the upcall xlator, and increase the md-cache timeout to max
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 indexing on

##  Start the volume
TEST $CLI volume start $V0
TEST $CLI volume quota $V0 enable

##  Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

##  Create directory and files from the M0
TEST touch $M0/file1
TEST mv $M0/file1 $M0/file2

cleanup;
