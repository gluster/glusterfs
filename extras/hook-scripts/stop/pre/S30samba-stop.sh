#! /bin/bash

PROGNAME="Ssamba-stop"
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

function del_samba_export () {
        volname=$1
        cp /etc/samba/smb.conf /tmp/smb.conf
        sed -i "/gluster-$volname/,/^$/d" /tmp/smb.conf &&\
                mv /tmp/smb.conf /etc/samba/smb.conf
}

function umount_volume () {
        volname=$1
        mnt_pre=$2
        umount -l $mnt_pre/$volname
}

function remove_fstab_entry () {
	volname=$1
	mntpt=$2
	mntent="`hostname`:/$volname $mntpt glusterfs defaults,transport=tcp 0 0"
	esc_mntent=$(echo -e "$mntent" | sed 's/\//\\\//g')
	exists=`grep "$mntent" /etc/fstab`
	if [ "$exists" != " " ]
	then
		sed -i /"$esc_mntent"/d /etc/fstab
	fi
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
del_samba_export $VOL
umount_volume $VOL $MNT_PRE
remove_fstab_entry $VOL $MNT_PRE/$VOL
sighup_samba
