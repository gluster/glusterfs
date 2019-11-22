#!/bin/bash

#Need to be copied to hooks/<HOOKS_VER>/start/post

#TODO: All gluster and samba paths are assumed for fedora like systems.
#Some efforts are required to make it work on other distros.

#The preferred way of creating a smb share of a gluster volume has changed.
#The old method was to create a fuse mount of the volume and share the mount
#point through samba.
#
#New method eliminates the requirement of fuse mount and changes in fstab.
#glusterfs_vfs plugin for samba makes call to libgfapi to access the volume.
#
#This hook script automagically creates shares for volume on every volume start
#event by adding the entries in smb.conf file and sending SIGHUP to samba.
#
#In smb.conf:
#glusterfs vfs plugin has to be specified as required vfs object.
#Path value is relative to the root of gluster volume;"/" signifies complete
#volume.

PROGNAME="Ssamba-start"
OPTSPEC="volname:,gd-workdir:,version:,volume-op:,first:"
VOL=
CONFIGFILE=
LOGFILEBASE=
PIDDIR=
GLUSTERD_WORKDIR=
VERSION=
VOLUME_OP=
FIRST=

function parse_args () {
        ARGS=$(getopt -o '' -l $OPTSPEC -n $PROGNAME -- "$@")
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
                --version)
                    shift
                    VERSION=$1
                    ;;
                --volume-op)
                    shift
                    VOLUME_OP=$1
                    ;;
                --first)
                    shift
                    FIRST=$1
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
        cmdout=$(smbd -b 2> /dev/null)
        CONFIGFILE=$(echo "$cmdout" | grep CONFIGFILE | awk '{print $2}')
        if [ -z "$CONFIGFILE" ]; then
                echo "Samba is not installed"
                exit 1
        fi
        PIDDIR=$(echo "$cmdout" | grep PIDDIR | awk '{print $2}')
        LOGFILEBASE=$(echo "$cmdout" | grep 'LOGFILEBASE' | awk '{print $2}')
}

function add_samba_share () {
        volname=$1
        STRING="\n[gluster-$volname]\n"
        STRING+="comment = For samba share of volume $volname\n"
        STRING+="vfs objects = glusterfs\n"
        STRING+="glusterfs:volume = $volname\n"
        STRING+="glusterfs:logfile = $LOGFILEBASE/glusterfs-$volname.%%M.log\n"
        STRING+="glusterfs:loglevel = 7\n"
        STRING+="path = /\n"
        STRING+="read only = no\n"
        STRING+="kernel share modes = no\n"
        printf "$STRING"  >> "${CONFIGFILE}"
}

function sighup_samba () {
        pid=$(cat "${PIDDIR}/smbd.pid" 2> /dev/null)
        if [ "x$pid" != "x" ]
        then
                kill -HUP "$pid";
        else
                service smb condrestart
        fi
}

function get_smb () {
        volname=$1
        uservalue=

        usercifsvalue=$(grep user.cifs "$GLUSTERD_WORKDIR"/vols/"$volname"/info |\
                        cut -d"=" -f2)
        usersmbvalue=$(grep user.smb "$GLUSTERD_WORKDIR"/vols/"$volname"/info |\
                       cut -d"=" -f2)

        if [ -n "$usercifsvalue" ]; then
                if [ "$usercifsvalue" = "enable" ] || [ "$usercifsvalue" = "on" ]; then
                        uservalue="enable"
                fi
        fi

        if [ -n "$usersmbvalue" ]; then
                if [ "$usersmbvalue" = "enable" ] || [ "$usersmbvalue" = "on" ]; then
                        uservalue="enable"
                fi
        fi

        echo "$uservalue"
}

parse_args "$@"

value=$(get_smb "$VOL")

if [ -z "$value" ] || [ "$value" != "enable" ]; then
        exit 0
fi

#Find smb.conf, smbd pid directory and smbd logfile path
find_config_info

if ! grep --quiet "\[gluster-$VOL\]" "${CONFIGFILE}" ; then
        add_samba_share "$VOL"
else
        sed -i '/\[gluster-'"$VOL"'\]/,/^$/!b;/available = no/d' "${CONFIGFILE}"
fi
sighup_samba
