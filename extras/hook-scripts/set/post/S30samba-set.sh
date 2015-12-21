#!/bin/bash

#Need to be copied to hooks/<HOOKS_VER>/set/post/

#TODO: All gluster and samba paths are assumed for fedora like systems.
#Some efforts are required to make it work on other distros.

#The preferred way of creating a smb share of a gluster volume has changed.
#The old method was to create a fuse mount of the volume and share the mount
#point through samba.
#
#New method eliminates the requirement of fuse mount and changes in fstab.
#glusterfs_vfs plugin for samba makes call to libgfapi to access the volume.
#
#This hook script enables user to enable or disable smb share by volume set
#option. Keys "user.cifs" and "user.smb" both are valid, but user.smb is
#preferred.


PROGNAME="Ssamba-set"
OPTSPEC="volname:,gd-workdir:"
VOL=
CONFIGFILE=
LOGFILEBASE=
PIDDIR=
GLUSTERD_WORKDIR=
USERSMB_SET=""
USERCIFS_SET=""

function parse_args () {
        ARGS=$(getopt -l $OPTSPEC  --name $PROGNAME -o "o:" -- $@)
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
                            "user.cifs")
                                USERCIFS_SET="YES"
                                ;;
                            "user.smb")
                                USERSMB_SET="YES"
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
        cmdout=`smbd -b | grep smb.conf`
        if [ $? -ne 0 ]; then
                echo "Samba is not installed"
                exit 1
        fi
        CONFIGFILE=`echo $cmdout | awk '{print $2}'`
        PIDDIR=`smbd -b | grep PIDDIR | awk '{print $2}'`
        LOGFILEBASE=`smbd -b | grep 'LOGFILEBASE' | awk '{print $2}'`
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
        STRING+="guest ok = yes\n"
        printf "$STRING"  >> ${CONFIGFILE}
}

function sighup_samba () {
        pid=`cat ${PIDDIR}/smbd.pid`
        if [ "x$pid" != "x" ]
        then
                kill -HUP "$pid";
        else
                service smb condrestart
        fi
}

function del_samba_share () {
        volname=$1
        sed -i "/\[gluster-$volname\]/,/^$/d" ${CONFIGFILE}
}

function is_volume_started () {
        volname=$1
        echo "$(grep status $GLUSTERD_WORKDIR/vols/"$volname"/info |\
                cut -d"=" -f2)"
}

function get_smb () {
        volname=$1
        uservalue=

        usercifsvalue=$(grep user.cifs $GLUSTERD_WORKDIR/vols/"$volname"/info |\
                        cut -d"=" -f2)
        usersmbvalue=$(grep user.smb $GLUSTERD_WORKDIR/vols/"$volname"/info |\
                       cut -d"=" -f2)

        if [[ $usercifsvalue = "disable" || $usersmbvalue = "disable" ]]; then
                uservalue="disable"
        fi
        echo "$uservalue"
}

parse_args $@
if [ "0" = $(is_volume_started "$VOL") ]; then
    exit 0
fi


if [[ "$USERCIFS_SET" = "YES" || "$USERSMB_SET" = "YES" ]]; then
    #Find smb.conf, smbd pid directory and smbd logfile path
    find_config_info

    if [ $(get_smb "$VOL") = "disable" ]; then
        del_samba_share $VOL
        sighup_samba
    else
        if ! grep --quiet "\[gluster-$VOL\]" ${CONFIGFILE} ; then
            add_samba_share $VOL
            sighup_samba
        fi
    fi
fi
