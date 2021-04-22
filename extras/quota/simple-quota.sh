#!/bin/bash

INTERVAL=5
MOUNT_DIR=
CONFIG=
VOL=
WORKDIR="/var/lib/glusterd"

function get_set_total_usage()
{
    path=$1
    if [ -d $path ]; then
	# this has the risk of returning success always.
	# Need to come up with better solution.
	# may be a libgfapi based solution?
	used_size=$(df --block-size=1 --output=used $path | tail -n1);
	if [ $used_size -ne 0 ]; then
	    setfattr -n glusterfs.quota.total-usage -v $used_size $path;
	fi
    fi
}

function parse_args () {
        ARGS=$(getopt -o 'w:v:c:' -- "$@")
        eval set -- "$ARGS"

        while true; do
            case $1 in
                -v)
                    shift
                    VOL=$1
                    ;;
                -c)
                    shift
                    CONFIG=$1
                    ;;
                -w)
                    shift
                    WORKDIR=$1
                    ;;
                --)
                    shift
                    break
                    ;;
                *)
                    shift
                    break
                    ;;
            esac
            shift
        done

	if [ -z $VOL ]; then
	    echo "-v (Volume name) option is not given. Exiting";
	    exit 1;
	fi

	if [ -d $WORKDIR/vols/$VOL ]; then
	    if [ -z $CONFIG ] ; then
		CONFIG=$WORKDIR/vols/$VOL/simple-quota.conf
	    fi
	    touch $CONFIG;
	    if [ $? -ne 0 ]; then
		echo "failed to check config file. Exiting";
		exit 1;
	    fi
	else
	    echo "Volume $VOL doesn't exist. Exiting";
	    exit 1;
	fi
}

function main()
{

    parse_args "$@";

    # Get the volume mounted with the pre-coded PID so
    # the translator recognises the commands from the
    # mount.

    # TODO: currently expecting mount to happen from volume set command,
    # which can be done here too.

    number_of_paths=$(wc -l $CONFIG | cut -f1 -d' ');
    if [ $number_of_paths -eq 0 ]; then
	echo "Quota limit is not set on any paths. Exiting."
	exit 0;
    fi

    MOUNT_DIR=$(grep "mount-point" $CONFIG | cut -f2 -d':');

    while true; do
	sleep $INTERVAL;
	while read line; do
	    read path limit < <(echo "$line" | tr ":" " ");
	    if [ "$path" = "mount-point" ]; then
		# Ignore the mount-point line
		continue;
	    fi

	    get_set_total_usage "$MOUNT_DIR/$path";
	done < $CONFIG
    done
}


main "$@";
