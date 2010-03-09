#!/bin/sh

# This script can be used to sync disk usage before activating quotas on GlusterFS.
# There are two scenarios where quotas are used and this script has to be used accordingly - 
#
# 1. Server side
# The script needs to be run with backend export directory as the argument.  This script updates
# the current disk usage of the backend directory in a way that is intelligible to the GlusterFS quota 
# translator. Make sure you run this script before starting glusterfsd (GlusterFS server process):
#	* for the first time
#	* after any server outage (reboot, etc.)

# 2. Client side
# The script needs to be run with the client mount point as the argument. It updates the current disk
# of the GlusterFS volume is a way that is intelligible to the GlusterFS quota translator. Make sure
# you run this script after a fresh mount of the GlusterFS volume on the client:
#	* For the first time
#	* After any client outage (reboot, remount, etc.)

# Please note that this script is dependent on the 'attr' package, more specifically 'setfattr' to set 
# extended attributes of files.
# GlusterFS
# 
# (c) 2010 Gluster Inc <http://www.gluster.com/> 

PROGRAM_NAME="disk_usage_sync.sh"

#check if setfattr is available
check_for_attr ()
{
    `command -v setfattr >/dev/null`
    if [ "${?}" -gt 0 ]; then
	echo >&2 "This script requires the 'attr' package to run. Either it has not been installed or is not present currently in the system path."
	exit 1
    fi

}

usage () {
    echo >&2 "$PROGRAM_NAME - Command used to sync disk usage information before activating quotas on GlusterFS.

usage: $PROGRAM_NAME <target-directory>"

exit 1
}

TARGET_DIR=$1
EXT_ATTR_NAME="trusted.glusterfs-quota-du"

#output of du in number of bytes
get_disk_usage ()
{
    if [ ! -d $TARGET_DIR ]; then
	echo >&2 "Error: $TARGET_DIR does not exist."
	exit 1
    fi

    DISK_USAGE=`du -bc $TARGET_DIR | grep 'total' | cut -f1`
    if [ "${?}" -gt 0 ]; then
	exit 1
    fi

}

#set the extended attribute of the root directory with du size in bytes
set_disk_usage ()
{
   ` setfattr -n $EXT_ATTR_NAME -v $DISK_USAGE $TARGET_DIR`
    if [ "${?}" -gt 0 ]; then
	exit 1
    fi
}

main ()
{
    [ $# -lt 1 ] && usage

    check_for_attr

    get_disk_usage
    set_disk_usage

    printf "Disk Usage information has been sync'd successfully.\n"
}

main "$@"
