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
OPTSPEC="volname:"
VOL=

enable_smb=""

function parse_args () {
        ARGS=$(getopt -l $OPTSPEC  -o "o" -name $PROGNAME $@)
        eval set -- "$ARGS"

        while true; do
            case $1 in
            --volname)
                shift
                VOL=$1
                ;;
            *)
                shift
                for pair in $@; do
                        read key value < <(echo "$pair" | tr "=" " ")
                        case "$key" in
                            "user.cifs")
                                    enable_smb=$value
                                    ;;
                            "user.smb")
                                    enable_smb=$value
                                    ;;
                            *)
                                    ;;
                        esac
                done

                shift
                break
                ;;
            esac
            shift
        done
}

function add_samba_share () {
        volname=$1
        STRING="\n[gluster-$volname]\n"
        STRING+="comment = For samba share of volume $volname\n"
        STRING+="vfs objects = glusterfs\n"
        STRING+="glusterfs:volume = $volname\n"
        STRING+="glusterfs:logfile = /var/log/samba/glusterfs-$volname.%%M.log\n"
        STRING+="glusterfs:loglevel = 7\n"
        STRING+="path = /\n"
        STRING+="read only = no\n"
        STRING+="guest ok = yes\n"
        printf "$STRING"  >> /etc/samba/smb.conf
}

function sighup_samba () {
        pid=`cat /var/run/smbd.pid`
        if [ "x$pid" != "x" ]
        then
                kill -HUP "$pid";
        else
                /etc/init.d/smb start
        fi
}

function del_samba_share () {
        volname=$1
        sed -i "/\[gluster-$volname\]/,/^$/d" /etc/samba/smb.conf
}

function is_volume_started () {
        volname=$1
        echo "$(grep status /var/lib/glusterd/vols/"$volname"/info |\
                cut -d"=" -f2)"
}

parse_args $@
if [ "0" = $(is_volume_started "$VOL") ]; then
    exit 0
fi

if [ "$enable_smb" = "enable" ]; then
    if ! grep --quiet "\[gluster-$VOL\]" /etc/samba/smb.conf ; then
            add_samba_share $VOL
            sighup_samba
    fi

elif [ "$enable_smb" = "disable" ]; then
    del_samba_share $VOL
    sighup_samba
fi
