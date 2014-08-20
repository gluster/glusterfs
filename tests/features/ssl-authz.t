#!/bin/bash

. $(dirname $0)/../include.rc

ping_file () {
	echo hello > $1 2> /dev/null
}

SSL_BASE=/etc/ssl
SSL_KEY=$SSL_BASE/glusterfs.key
SSL_CERT=$SSL_BASE/glusterfs.pem
SSL_CA=$SSL_BASE/glusterfs.ca

cleanup;
rm -f $SSL_BASE/glusterfs.*
mkdir -p $B0/1
mkdir -p $M0

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST openssl genrsa -out $SSL_KEY 1024
TEST openssl req -new -x509 -key $SSL_KEY -subj /CN=Anyone -out $SSL_CERT
ln $SSL_CERT $SSL_CA

TEST $CLI volume create $V0 $H0:$B0/1
TEST $CLI volume set $V0 server.ssl on
TEST $CLI volume set $V0 client.ssl on
TEST $CLI volume set $V0 auth.ssl-allow Anyone
TEST $CLI volume start $V0

# This mount should WORK.
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
TEST ping_file $M0/before
TEST umount $M0

# Change the authorized user name.  Note that servers don't pick up changes
# automagically like clients do, so we have to stop/start ourselves.
TEST $CLI volume stop $V0
TEST $CLI volume set $V0 auth.ssl-allow NotYou
TEST $CLI volume start $V0

# This mount should FAIL because the identity given by our certificate does not
# match the allowed user.  In other words, authentication works (they know who
# we are) but authorization doesn't (we're not the right person).
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0

# Looks like /*/bin/glusterfs isn't returning error status correctly (again).
# Actually try doing something to get a real error.
TEST ! ping_file $M0/after

cleanup;
