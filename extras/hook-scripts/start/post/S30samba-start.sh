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


parse_args $@
add_samba_export $VOL $MNT_PRE
sleep 5
mount -t glusterfs `hostname`:$volname $mnt_pre/$volname
sighup_samba
