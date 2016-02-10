#!/bin/sh

##---------------------------------------------------------------------------
## This script updates the 'limit-set' xattr on the newly added node. Please
## refer hook-scripts/add-brick/pre/S28Quota-root-xattr-heal.sh for the complete
## description.
## Do the following only if limit configured on root.
## 1. Do an auxiliary mount.
## 2. Get 'limit-set' xattr on root
## 3. Set xattrs with the same value on the root.
## 4. Disable itself
##---------------------------------------------------------------------------

QUOTA_LIMIT_XATTR="trusted.glusterfs.quota.limit-set"
QUOTA_OBJECT_LIMIT_XATTR="trusted.glusterfs.quota.limit-objects"
MOUNT_DIR=`mktemp -d -t ${0##*/}.XXXXXX`;
OPTSPEC="volname:,version:,gd-workdir:,volume-op:"
PROGNAME="Quota-xattr-heal-add-brick"
VOL_NAME=
VERSION=
VOLUME_OP=
GLUSTERD_WORKDIR=
ENABLED_NAME="S28Quota-root-xattr-heal.sh"

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

disable_and_exit ()
{
        if [ -e "$ENABLED_STATE" ]
        then
                unlink $ENABLED_STATE;
                exit $?
        fi

        exit 0
}

get_and_set_xattr ()
{
        XATTR=$1

        VALUE=$(getfattr -n $XATTR -e hex --absolute-names $MOUNT_DIR 2>&1)
        RET=$?
        if [ 0 -eq $RET ]; then
                VALUE=$(echo $VALUE | grep $XATTR | awk -F'=' '{print $NF}')
                setfattr -n $XATTR -v $VALUE $MOUNT_DIR;
                RET=$?
        else
                echo $VALUE | grep -iq "No such attribute"
                if [ 0 -eq $? ]; then
                        RET=0
                fi
        fi

        return $RET;
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
        --version)
            shift
            VERSION=$1
            ;;
        --gd-workdir)
            shift
            GLUSTERD_WORKDIR=$1
            ;;
        --volume-op)
            shift
            VOLUME_OP=$1
            ;;
        *)
            shift
            break
            ;;
    esac
    shift
done
##----------------------------------------

ENABLED_STATE="$GLUSTERD_WORKDIR/hooks/$VERSION/$VOLUME_OP/post/$ENABLED_NAME"

## Is quota enabled?
FLAG=`grep "^features.quota=" $GLUSTERD_WORKDIR/vols/$VOL_NAME/info \
      | awk -F'=' '{print $NF}'`;
if [ "$FLAG" != "on" ]
then
        disable_and_exit
fi

## -----------------------------------
## Mount the volume in temp directory.
## -----------------------------------
glusterfs -s localhost --volfile-id=$VOL_NAME --client-pid=-42 $MOUNT_DIR;
if [ 0 -ne $? ]
then
        exit $?;
fi
## -----------------------------------

RET1=$(get_and_set_xattr $QUOTA_LIMIT_XATTR)
RET2=$(get_and_set_xattr $QUOTA_OBJECT_LIMIT_XATTR)

## Clean up and exit
cleanup_mountpoint;

if [ $RET1 -ne 0 -o $RET2 -ne 0 ]; then
        exit 1
fi

disable_and_exit;
