#! /bin/bash

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


function remove_fstab_entry () {
        mntpt=$1
        fstab="/etc/fstab"
        exists=`grep "$mntpt" ${fstab}`
        esc_mntpt=$(echo -e $mntpt | sed 's/\//\\\//g')
        if [ "$exists" != " " ]
        then
            sed -i /"$esc_mntpt"/d $fstab
            exists=`grep "$mntpt" ${fstab}`
            if [ "$exists" != " " ]
            then
                echo "fstab entry cannot be removed for unknown reason"
                exit 1
            fi
        fi
}

parse_args $@
if [ "$META" = "$VOL" ]
then
        umount "$CTDB_MNT"
        chkconfig ctdb off
        remove_fstab_entry $CTDB_MNT
fi
