#!/bin/bash
#Need to be copied to hooks/<HOOKS_VER>/start/post

PROGNAME="Ssamba-start"
OPTSPEC="volname:,mnt:"
VOL=
#FIXME: glusterd hook interface will eventually provide mntpt prefix as
# command line arg
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
        --mnt)
         shift
         MNT_PRE=$1
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

function mount_volume () {
        volname=$1
        mnt_pre=$2
        #Mount shouldn't block on glusterd to fetch volfile, hence the 'bg'
        mount -t glusterfs `hostname`:$volname $mnt_pre/$volname &
}

function sighup_samba () {
        pid=`cat /var/run/smbd.pid`
        if [ $pid != "" ]
        then
                kill -HUP $pid;
        else
                /etc/init.d/smb condrestart
        fi
}


parse_args $@
add_samba_export $VOL $MNT_PRE
mount_volume $VOL $MNT_PRE
sighup_samba
