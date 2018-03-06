#!/bin/bash

##---------------------------------------------------------------------------
## This script runs the self-heal of the directories which are expected to
## be present as they are mounted as subdirectory mounts.
##---------------------------------------------------------------------------

MOUNT_DIR=`mktemp -d -t ${0##*/}.XXXXXX`;
OPTSPEC="volname:,version:,gd-workdir:,volume-op:"
PROGNAME="add-brick-create-subdir"
VOL_NAME=test
GLUSTERD_WORKDIR="/var/lib/glusterd"

cleanup_mountpoint ()
{
        umount -f $MOUNT_DIR;
        if [ 0 -ne $? ]
        then
                return $?
        fi

        rmdir $MOUNT_DIR;
        if [ 0 -ne $? ]
        then
                return $?
        fi
}

##------------------------------------------
## Parse the arguments
##------------------------------------------
ARGS=$(getopt -l $OPTSPEC  -name $PROGNAME $@)
eval set -- "$ARGS"

while true;
do
    case $1 in
        --volname)
            shift
            VOL_NAME=$1
            ;;
        --gd-workdir)
            shift
            GLUSTERD_WORKDIR=$1
            ;;
	--version)
	    shift
	    ;;
	--volume-op)
	    shift
	    ;;
	*)
	    shift
	    break
	    ;;
    esac
    shift
done

## See if we have any subdirs to be healed before going further
subdirs=$(grep 'auth.allow' ${GLUSTERD_WORKDIR}/vols/${VOL_NAME}/info | cut -f2 -d'=' | tr ',' '\n' | cut -f1 -d'(');

if [ -z ${subdirs} ]; then
    rmdir $MOUNT_DIR;
    exit 0;
fi

##----------------------------------------
## Mount the volume in temp directory.
## -----------------------------------
glusterfs -s localhost --volfile-id=$VOL_NAME --client-pid=-50 $MOUNT_DIR;
if [ 0 -ne $? ]
then
    exit $?;
fi

## -----------------------------------
# Do the 'stat' on all the directory for now. Ideal fix is to look at subdir
# list from 'auth.allow' option and only stat them.
for subdir in ${subdirs}
do
    stat ${MOUNT_DIR}/${subdir} > /dev/null;
done

## Clean up and exit
cleanup_mountpoint;
