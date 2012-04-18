#! /bin/bash
#non-portable - RHS-2.0 only
# - The script mounts the 'meta-vol' on start 'event' on a known
#   directory (eg. /gluster/lock)
# - Adds the necessary configuration changes for ctdb in smb.conf and
#   restarts smb service.
# - P.S: There are other 'tasks' that need to be done outside this script
#   to get CTDB based failover up and running.

SMB_CONF=/etc/samba/smb.conf

CTDB_MNT=/gluster/lock
PROGNAME="ctdb"
OPTSPEC="volname:"
VOL=
# $META is the volume that will be used by CTDB as a shared filesystem.
# It is not desirable to use this volume for storing 'data' as well.
# META is set to 'all' (viz. a keyword and hence not a legal volume name)
# to prevent the script from running for volumes it was not intended.
# User needs to set META to the volume that serves CTDB lockfile.
META="all"

function sighup_samba () {
        pid=`cat /var/run/smbd.pid`
        if [ $pid != " " ]
        then
                kill -HUP $pid;
        else
                /etc/init.d/smb start
        fi
}

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

function add_glusterfs_ctdb_options () {
        PAT="Share Definitions"
        GLUSTER_CTDB_CONFIG="# ctdb config for glusterfs\n\tclustering = yes\n\tidmap backend = tdb2\n\tprivate dir = "$CTDB_MNT"\n"

        sed -i /"$PAT"/i\ "$GLUSTER_CTDB_CONFIG" $SMB_CONF
}

parse_args $@
if [ "$META" = "$VOL" ]
then
        add_glusterfs_ctdb_options
        sighup_samba
        mount -t glusterfs `hostname`:$VOL "$CTDB_MNT" &
fi

