#!/bin/bash

#This script is called by glusterd when in case of
#reboot.An export file specific to a volume
#is copied in GANESHA_DIR/exports from online node.

# Try loading the config from any of the distro
# specific configuration locations
if [ -f /etc/sysconfig/ganesha ]
        then
        . /etc/sysconfig/ganesha
fi
if [ -f /etc/conf.d/ganesha ]
        then
        . /etc/conf.d/ganesha
fi
if [ -f /etc/default/ganesha ]
        then
        . /etc/default/ganesha
fi

GANESHA_DIR=${1%/}
VOL=$2
CONF=
host=$(hostname -s)
SECRET_PEM="/var/lib/glusterd/nfs/secret.pem"

function check_cmd_status()
{
        if [ "$1" != "0" ]
                 then
                 rm -rf $GANESHA_DIR/exports/export.$VOL.conf
                 exit 1
        fi
}


if [ ! -d "$GANESHA_DIR/exports" ];
        then
        mkdir $GANESHA_DIR/exports
        check_cmd_status `echo $?`
fi

function find_rhel7_conf
{
 while [[ $# > 0 ]]
        do
                key="$1"
                case $key in
                        -f)
                         CONFFILE="$2"
                         ;;
                         *)
                         ;;
                 esac
                 shift
         done
}

if [ -z $CONFFILE ]; then
        find_rhel7_conf $OPTIONS

fi
CONF=${CONFFILE:-/etc/ganesha/ganesha.conf}

#remove the old export entry from NFS-Ganesha
#if already exported
dbus-send --type=method_call --print-reply --system \
          --dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr \
          org.ganesha.nfsd.exportmgr.ShowExports \
    | grep -w -q "/$VOL"
if [ $? -eq 0 ]; then
        removed_id=`cat $GANESHA_DIR/exports/export.$VOL.conf |\
                grep Export_Id | awk -F"[=,;]" '{print$2}' | tr -d '[[:space:]]'`

        dbus-send --print-reply --system --dest=org.ganesha.nfsd \
        /org/ganesha/nfsd/ExportMgr org.ganesha.nfsd.exportmgr.RemoveExport \
        uint16:$removed_id 2>&1
fi

ha_servers=$(pcs status | grep "Online:" | grep -o '\[.*\]' | sed -e 's/\[//' | sed -e 's/\]//')
IFS=$' '
for server in ${ha_servers} ; do
        current_host=`echo $server | cut -d "." -f 1`
        if [ $host != $current_host ]
        then
                scp -oPasswordAuthentication=no -oStrictHostKeyChecking=no -i \
                ${SECRET_PEM} $server:$GANESHA_DIR/exports/export.$VOL.conf \
                $GANESHA_DIR/exports/export.$VOL.conf
                break
        fi
done

if ! (cat $CONF | grep  $VOL.conf\"$ )
then
echo "%include \"$GANESHA_DIR/exports/export.$VOL.conf\"" >> $CONF
fi
