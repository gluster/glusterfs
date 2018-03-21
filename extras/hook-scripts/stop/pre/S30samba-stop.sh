#! /bin/bash

#Need to be copied to hooks/<HOOKS_VER>/stop/pre

#TODO: All gluster and samba paths are assumed for fedora like systems.
#Some efforts are required to make it work on other distros.

#The preferred way of creating a smb share of a gluster volume has changed.
#The old method was to create a fuse mount of the volume and share the mount
#point through samba.
#
#New method eliminates the requirement of fuse mount and changes in fstab.
#glusterfs_vfs plugin for samba makes call to libgfapi to access the volume.
#
#This hook script automagically removes shares for volume on every volume stop
#event by removing the volume related entries(if any) in smb.conf file.

PROGNAME="Ssamba-stop"
OPTSPEC="volname:,last:"
VOL=
CONFIGFILE=
PIDDIR=
LAST=

function parse_args () {
        ARGS=$(getopt -o '' -l $OPTSPEC -n $PROGNAME -- "$@")
        eval set -- "$ARGS"

        while true; do
            case $1 in
                --volname)
                    shift
                    VOL=$1
                    ;;
                --last)
                    shift
                    LAST=$1
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
        if [ $? -ne 0 ];then
                echo "Samba is not installed"
                exit 1
        fi
        CONFIGFILE=`echo $cmdout | awk '{print $2}'`
        PIDDIR=`smbd -b | grep PIDDIR | awk '{print $2}'`
}

function deactivate_samba_share () {
        volname=$1
        sed -i -e '/^\[gluster-'"$volname"'\]/{ :a' -e 'n; /available = no/H; /^$/!{$!ba;}; x; /./!{ s/^/available = no/; $!{G;x}; $H; }; s/.*//; x; };' ${CONFIGFILE}
}

function sighup_samba () {
        pid=`cat ${PIDDIR}/smbd.pid`
        if [ "x$pid" != "x" ]
        then
                kill -HUP $pid;
        else
                service smb condrestart
        fi
}

parse_args "$@"
find_config_info
deactivate_samba_share $VOL
sighup_samba
