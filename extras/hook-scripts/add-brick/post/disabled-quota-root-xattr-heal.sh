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

QUOTA_CONFIG_XATTR="trusted.glusterfs.quota.limit-set";
MOUNT_DIR=`mktemp --directory --tmpdir`;
OPTSPEC="volname:,version:,gd-workdir:,volume-op:"
PROGNAME="Quota-xattr-heal-add-brick"
VOL_NAME=
VERSION=
VOLUME_OP=
GLUSTERD_WORKING_DIR=
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
                 GLUSTERD_WORKING_DIR=$1
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

ENABLED_STATE="$GLUSTERD_WORKING_DIR/hooks/$VERSION/$VOLUME_OP/post/$ENABLED_NAME"


FLAG=`gluster volume quota $VOL_NAME list / 2>&1 | grep \
      '\(No quota configured on volume\)\|\(Limit not set\)'`;
if ! [ -z $FLAG ]
then
        ls $ENABLED_STATE;
        RET=$?
        if [ 0 -eq $RET ]
        then
                unlink $ENABLED_STATE;
                exit $?
        fi

        exit $RET;
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

## ------------------
## Getfattr the value
## ------------------
VALUE=`getfattr -n "$QUOTA_CONFIG_XATTR" -e hex --absolute-names $MOUNT_DIR \
       2>&1 | grep $QUOTA_CONFIG_XATTR | awk -F'=' '{print $2}'`
RET=$?
if [ 0 -ne $RET ]
then
        ## Clean up and exit
        cleanup_mountpoint;

        exit $RET;
fi
## ------------------

## ---------
## Set xattr
## ---------
setfattr -n "$QUOTA_CONFIG_XATTR" -v $VALUE $MOUNT_DIR;
RET=$?
if [ 0 -ne $RET ]
then
        ## Clean up and exit
        cleanup_mountpoint;

        exit $RET;
fi
## ---------

cleanup_mountpoint;

## Disable
ls $ENABLED_STATE;
RET=$?
if [ 0 -eq $RET ]
then
        unlink $ENABLED_STATE;
        exit $?
fi
exit $?
