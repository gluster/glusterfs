#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

log_base=$($CLI --print-logdir)
log_id=${B0}/${V0}-0
log_id=${log_id:1}     # Remove initial slash
log_id=${log_id//\//-} # Replace remaining slashes with dashes

_check_sizes () {
	local n=0
	local sz
	local total_sz=0

	# We don't care about the sizes of the meta files.  That would be
	# embedding too much of the implementation into the test.
	n=$(ls ${log_base}/${log_id}-meta-*.jnl | wc -l)
	[ $n = 2 ] || return 1

	# We *do* care about the sizes of the data files, which should exactly
	# reflect the amount of data written via dd.
	n=0
	while read sz name; do
                G_LOG "found journal ${name} size ${sz}MB"
		n=$((n+1))
		total_sz=$((total_sz+sz))
	done < <(du -sm ${log_base}/${log_id}-data-*.jnl)
	[ $n = 2 ] || return 1
	# On our CentOS and NetBSD regression-test systems, but not on my Fedora
	# development system, each file ends up being slightly larger than its
	# data size because of metadata, and 'du' rounds that up to a full extra
	# megabyte.  We'll allow either result, because what we're really
	# looking for is a complete failure to roll over from one file to
	# another at the appropriate size.
	[ $total_sz = 20 -o $total_sz = $((n+20)) ] || return 1

	return 0
}

check_sizes () {
	set -x
	_check_sizes
	ret=$?
	set +x
	return ret
}

if [ x"$OSTYPE" = x"NetBSD" ]; then
        CREAT_OFLAG="creat,"
else
        CREAT_OFLAG=""
fi

TEST rm -f ${log_base}/${log_id}-*.log
TEST glusterd
TEST pidof glusterd

# Get a simple volume set up and mounted with FDL active.
TEST $CLI volume create $V0 ${H0}:${B0}/${V0}-0
TEST $CLI volume set $V0 changelog.changelog off
TEST $CLI volume set $V0 features.fdl on
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 $M0

# Generate some I/O and unmount/stop so we can see log sizes.
TEST dd if=/dev/zero of=$M0/twentyMB bs=1048576 count=20 \
     oflag=${CREAT_OFLAG}sync
TEST umount $M0
TEST $CLI volume stop $V0

TEST _check_sizes

cleanup
