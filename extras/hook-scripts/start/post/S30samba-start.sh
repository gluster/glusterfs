#!/bin/bash

PROGNAME="Ssamba-start"
OPTSPEC="volname:"
VOL=
MNT_PRE="/mnt/samba"

function parse_args () {
        ARGS=$(getopt -l $OPTSPEC  -name $PROGNAME $@)
        eval set -- "$ARGS"

        while true; do
        case $1 in
        --volname)
         shift
         VOL=$1
         ;;
        *)
         shift
         break
         ;;
        esac
        shift
        done
}

function add_samba_export () {
        volname=$1
        mnt_pre=$2
        mkdir -p $mnt_pre/$volname && \
        printf "\n[gluster-$volname]\ncomment=For samba export of volume $volname\npath=$mnt_pre/$volname\nread only=no\nguest ok=yes\n" >> /etc/samba/smb.conf
}

function sighup_samba () {
        pid=`cat /var/run/smbd.pid`
        if [ $pid != " " ]
        then
                kill -HUP $pid;
        else
                /etc/init.d/smb start
        fi
}

function add_fstab_entry () {
        volname=$1
        mntpt=$2
        mntent="`hostname`:/$volname $mntpt glusterfs defaults,transport=tcp 0 0"
        exists=`grep "$mntent" /etc/fstab`
        if [ "$exists" == "" ]
        then
            echo "$mntent" >> /etc/fstab
        fi
}


parse_args $@
add_samba_export $VOL $MNT_PRE
mkdir -p $MNT_PRE/$VOL
sleep 5
mount -t glusterfs `hostname`:$VOL $MNT_PRE/$VOL && \
        add_fstab_entry $VOL $MNT_PRE/$VOL
sighup_samba
