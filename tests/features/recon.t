#!/bin/bash

. $(dirname $0)/../traps.rc
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

log_base=$($CLI --print-logdir)
log_id=${B0}/${V0}-0
log_id=${log_id:1}     # Remove initial slash
log_id=${log_id//\//-} # Replace remaining slashes with dashes
FDL_META_FILE=${log_base}/${log_id}-meta-1.jnl
FDL_DATA_FILE=${log_base}/${log_id}-data-1.jnl

tmpdir=$(mktemp -d -t ${0##*/}.XXXXXX)
push_trapfunc "rm -rf $tmpdir"

write_file () {
	echo "peekaboo" > $1
}

TEST rm -f $FDL_META_FILE $FDL_DATA_FILE
TEST glusterd
TEST pidof glusterd

# Get a simple volume set up and mounted with FDL active.
TEST $CLI volume create $V0 ${H0}:${B0}/${V0}-0
TEST $CLI volume set $V0 features.fdl on
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 $M0

# Generate some I/O and then copy off the journal files for later.
TEST mkdir -p $M0/abc/def
TEST write_file $M0/abc/def/ghi
#EST chmod 314 $M0/abc/def/ghi
cp ${FDL_META_FILE} ${FDL_DATA_FILE} ${tmpdir}

# Get back to an empty state and unmount.
TEST rm -rf $M0/abc
TEST umount $M0

# Make sure we really are in an empty state.  Otherwise the tests below could
# pass just because we never cleaned up in the first place.
TEST [ ! -d ${B0}/${V0}-0/abc ]

# Create a stub volfile.
vol_file=${GLUSTERD_WORKDIR}/vols/${V0}/${V0}.${H0}.${log_id}.vol
vol_id_line=$(grep volume-id ${vol_file})
cat > ${tmpdir}/recon.vol << EOF
volume recon-posix
    type storage/posix
    option directory ${B0}/${V0}-0
${vol_id_line}
end-volume
EOF

TEST gf_recon ${tmpdir}/recon.vol ${tmpdir}/$(basename ${FDL_META_FILE}) \
				  ${tmpdir}/$(basename ${FDL_DATA_FILE})

TEST [ -d ${B0}/${V0}-0/abc/def ]
EXPECT "peekaboo" cat ${B0}/${V0}-0/abc/def/ghi
# TBD: test permissions, xattrs

cleanup
