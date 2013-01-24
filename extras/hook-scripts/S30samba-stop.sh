#! /bin/bash
#Need to be copied to hooks/<HOOKS_VER>/stop/post

PROGNAME="Ssamba-stop"
OPTSPEC="volname:,mnt:"
VOL=
#FIXME: gluster will eventually pass mnt prefix as command line argument
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
         echo $1
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
                cp /tmp/smb.conf /etc/samba/smb.conf
}

function umount_volume () {
        volname=$1
        mnt_pre=$2
        umount -l $mnt_pre/$volname
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
del_samba_export $VOL
umount_volume $VOL $MNT_PRE
sighup_samba
