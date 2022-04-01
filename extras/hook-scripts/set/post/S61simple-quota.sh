#!/bin/bash

# Need to be copied to hooks/<HOOKS_VER>/set/post/

# This hook script is provided as a guide-line for management layer implementation.
# This is good enough with current glusterd, but if one wants to use it in other
# projects, they can use it with different commands too.
#
# This hook script enables user to set simple-quota limit on a volume. 'user.simple-quota.limit'


MOUNT_DIR=`mktemp -d -t ${0##*/}.XXXXXX`;
PROGNAME="Ssimple-quota-set"
OPTSPEC="volname:,gd-workdir:"
VOL=
CONFIGFILE=
LOGFILEBASE=
PIDDIR=
GLUSTERD_WORKDIR=
LIMIT=

function parse_args () {
        ARGS=$(getopt -o 'o:' -l $OPTSPEC -n $PROGNAME -- "$@")
        eval set -- "$ARGS"

        while true; do
            case $1 in
                --volname)
                    shift
                    VOL=$1
                    ;;
                --gd-workdir)
                    shift
                    GLUSTERD_WORKDIR=$1
                    ;;
                --)
                    shift
                    break
                    ;;
                -o)
                    shift
                        read key value < <(echo "$1" | tr "=" " ")
                        case "$key" in
                            "user.simple-quota.limit")
				read path limit < <(echo "$value" | tr ":" " ")
				LIMIT=$limit
				PATH=$path
                                ;;
                            *)
                                ;;
                        esac
                    ;;
                *)
                    shift
                    break
                    ;;
            esac
            shift
        done
}

function find_config_info () {
    CONFIGFILE=$GLUSTERD_WORKDIR/vols/$volname/simple-quota.conf
    touch $CONFIGFILE
    if [ $? -ne 0 ]; then
	echo "failed to find config file. exiting";
	exit 1;
    fi
}

function add_path_to_config () {
        printf "$PATH : $LIMIT"  >> ${CONFIGFILE}
}

parse_args "$@"


if [ -n "$LIMIT" ]; then
    find_config_info $VOL

    number_of_paths=$(wc -l $CONFIGFILE | cut -f1 -d' ');
    start_script="";
    if [ $number_of_paths -eq 0 ]; then

	# TODO: Start a script which runs in while true loop also checking for 'df ' output
	# -14 defined in common-utils.h for simple-quota
	glusterfs -s localhost --volfile-id=$VOL --client-pid=-14 --process-name=quota $MOUNT_DIR;
	if [ 0 -ne $? ]; then
	    exit $?;
	fi
	echo "mount-point : $MOUNT_DIR" >> $CONFIGFILE;
    else
	MOUNT_DIR=$(grep mount-point $CONFIGFILE | cut -f2 -d':')
    fi

    # Set namespace on path
    setfattr -n trusted.glusterfs.namespace -v 1 $MOUNT_DIR/$PATH

    # set the limit
    setfattr -n trusted.gfs.squota.limit -v $LIMIT $MOUNT_DIR/$PATH;

    add_path_to_config $PATH $LIMIT

    # if [ -n $start_script ]; then
    #     bash ./simple-quota.sh
    # fi
fi
