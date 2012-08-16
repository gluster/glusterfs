#!/bin/bash

PROGNAME="Ssamba-set"
OPTSPEC="volname:"
VOL=
MNT_PRE="/mnt/samba"

enable_cifs=""

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
                            "user.cifs")    enable_cifs=$value;;
                            *) ;;
                        esac
                done

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
        if [ "$pid" != "" ]
        then
                kill -HUP "$pid";
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

function is_volume_started () {
        volname=$1
        echo "$(grep status /var/lib/glusterd/vols/"$volname"/info | cut -d"=" -f2)"
}

function get_cifs () {
        volname=$1
        echo "$(grep user.cifs /var/lib/glusterd/vols/"$volname"/info | cut -d"=" -f2)"
}

function mount_volume () {
	volname=$1
	mntpt=$2
	if [ "$(cat /proc/mounts | grep "$mntpt")" == "" ]; then
		mount -t glusterfs `hostname`:$volname $mntpt && \
				add_fstab_entry $volname $mntpt
	fi
}

parse_args $@
if [ "0" = $(is_volume_started "$VOL") ]; then
    exit 0
fi

if [ "$enable_cifs" = "enable" ]; then
    add_samba_export $VOL $MNT_PRE
    mkdir -p $MNT_PRE/$VOL
    sleep 5
    mount_volume $VOL $MNT_PRE/$VOL
    sighup_samba

elif [ "$enable_cifs" = "disable" ]; then
    del_samba_export $VOL
    umount_volume $VOL $MNT_PRE
    remove_fstab_entry $VOL $MNT_PRE/$VOL
    sighup_samba
fi
