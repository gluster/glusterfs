#!/bin/sh

###############################################################################
## ----------------------------------------------------------------------------
## The scripts
## I.   add-brick/pre/S28Quota-root-xattr-heal.sh (itself)
## II.  add-brick/post/disabled-root-xattr-heal.sh AND
## collectively archieves the job of healing the 'limit-set' xattr upon
## add-brick to the gluster volume.
##
## This script is the 'controlling' script. Upon add-brick this script enables
## the corresponding script based on the status of the volume.
## If volume is started - enable add-brick/post script
## else                 - enable start/post script.
##
## The enabling and disabling of a script is based on the glusterd's logic,
## that it only runs the scripts which starts its name with 'S'. So,
## Enable - symlink the file to 'S'*.
## Disable- unlink symlink
## ----------------------------------------------------------------------------
###############################################################################

OPTSPEC="volname:,version:,gd-workdir:,volume-op:"
PROGNAME="Quota-xattr-heal-add-brick-pre"
VOL_NAME=
GLUSTERD_WORKDIR=
VOLUME_OP=
VERSION=
ENABLED_NAME="S28Quota-root-xattr-heal.sh"
DISABLED_NAME="disabled-quota-root-xattr-heal.sh"

enable ()
{
        ln -sf $DISABLED_STATE $1;
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
        --volume-op)
            shift
            VOLUME_OP=$1
            ;;
        --version)
            shift
            VERSION=$1
            ;;
        *)
            shift
            break
            ;;
    esac
    shift
done
##----------------------------------------

DISABLED_STATE="$GLUSTERD_WORKDIR/hooks/$VERSION/add-brick/post/$DISABLED_NAME"
ENABLED_STATE_START="$GLUSTERD_WORKDIR/hooks/$VERSION/start/post/$ENABLED_NAME"
ENABLED_STATE_ADD_BRICK="$GLUSTERD_WORKDIR/hooks/$VERSION/add-brick/post/$ENABLED_NAME";

## Why to proceed if the required script itself is not present?
ls $DISABLED_STATE;
if [ 0 -ne $? ]
then
        exit $?;
fi

## Is quota enabled?
FLAG=`cat $GLUSTERD_WORKDIR/vols/$VOL_NAME/info | grep "^features.quota=" \
      | awk -F'=' '{print $NF}'`;
if [ "$FLAG" != "on" ]
then
        exit $EXIT_SUCCESS;
fi

## Is volume started?
FLAG=`cat $GLUSTERD_WORKDIR/vols/$VOL_NAME/info | grep "^status=" \
      | awk -F'=' '{print $NF}'`;
if [ "$FLAG" != "1" ]
then
        enable $ENABLED_STATE_START;
        exit $?
fi

enable $ENABLED_STATE_ADD_BRICK;
exit $?
