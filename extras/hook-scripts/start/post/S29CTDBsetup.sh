#! /bin/bash
#
# - The script mounts the 'meta-vol' on start 'event' on a known
#   directory (eg. /gluster/lock)
# - P.S: There are other 'tasks' that need to be done outside this script
#   to get CTDB based failover up and running.

CTDB_MNT=/gluster/lock
# Make sure ping-timeout is not default for CTDB volume
PING_TIMEOUT_SECS=10
PROGNAME="ctdb"
OPTSPEC="volname:"
HOSTNAME=`hostname`
MNTOPTS="_netdev,transport=tcp,xlator-option=*client*.ping-timeout=${PING_TIMEOUT_SECS}"
VOL=
# $META is the volume that will be used by CTDB as a shared filesystem.
# It is not desirable to use this volume for storing 'data' as well.
# META is set to 'all' (viz. a keyword and hence not a legal volume name)
# to prevent the script from running for volumes it was not intended.
# User needs to set META to the volume that serves CTDB lockfile.
META="all"

function parse_args () {
        ARGS=$(getopt -l $OPTSPEC  -name $PROGNAME $@)
        eval set -- "$ARGS"

        while true; do
            case $1 in
                --volname)
                    shift
                    VOL=$1
                ;;

                *)
                     shift
                     break
                ;;

            esac

            shift
        done
}

function add_fstab_entry () {
        volname=$1
        mntpt=$2
        mntopts="${MNTOPTS}"

        mntent="${HOSTNAME}:/${volname} ${mntpt} glusterfs ${mntopts} 0 0"
        exists=`grep "${mntpt}" /etc/fstab`
        if [ "$exists" == "" ]
        then
            echo "${mntent}" >> /etc/fstab
        fi
}

parse_args $@
if [ "$META" = "$VOL" ]
then
        mkdir -p $CTDB_MNT
        sleep 5
        mount -t glusterfs -o${MNTOPTS} ${HOSTNAME}:/$VOL "$CTDB_MNT" && \
            add_fstab_entry $VOL $CTDB_MNT
        chkconfig ctdb on
fi
