#/bin/bash

declare -i EXPORT_ID
GANESHA_DIR=${1%/}
OPTION=$2
VOL=$3
CONF=
CONFFILE=

function find_rhel7_conf
{
 while [[ $# > 0 ]]
        do
                key="$1"
                case $key in
                        -f)
                         CONFFILE="$2"
                         break;
                         ;;
                         *)
                         ;;
                 esac
                 shift
         done
}

cfgline=$(grep ^CONFFILE= /etc/sysconfig/ganesha)
eval $(echo ${cfgline} | grep -F ^CONFFILE=)

if [ -z $CONFFILE ]
        then
        cfgline=$(grep ^OPTIONS= /etc/sysconfig/ganesha)
        eval $(echo ${cfgline} | grep -F ^OPTIONS=)
        find_rhel7_conf $cfgline

fi

CONF=${CONFFILE:-/etc/ganesha/ganesha.conf}

function check_cmd_status()
{
        if [ "$1" != "0" ]
                 then
                 rm -rf $GANESHA_DIR/exports/export.$VOL.conf
                 sed -i /$VOL.conf/d $CONF
                 exit 1
        fi
}

#This function keeps track of export IDs and increments it with every new entry
function dynamic_export_add()
{
        count=`ls -l $GANESHA_DIR/exports/*.conf | wc -l`
        if [ "$count" = "1" ] ;
                then
                EXPORT_ID=2
        else
        #if [ -s /var/lib/ganesha/export_removed ];
        #               then
        #               EXPORT_ID=`head -1 /var/lib/ganesha/export_removed`
        #               sed -i -e "1d" /var/lib/ganesha/export_removed
        #               else

                 EXPORT_ID=`cat $GANESHA_DIR/.export_added`
                 check_cmd_status `echo $?`
        #fi
        fi
        for entry in `grep -n Export_Id  $GANESHA_DIR/exports/export.$VOL.conf \
        | awk -F":" '{print$1}'`
        do
                sed -e "$entry s/Export_Id.*/Export_Id=$EXPORT_ID ;/" -i \
                $GANESHA_DIR/exports/export.$VOL.conf
                check_cmd_status `echo $?`
                dbus-send  --system \
                --dest=org.ganesha.nfsd  /org/ganesha/nfsd/ExportMgr \
                org.ganesha.nfsd.exportmgr.AddExport  \
                string:$GANESHA_DIR/exports/export.$VOL.conf \
                string:"EXPORT(Export_Id=$EXPORT_ID)"
                EXPORT_ID=EXPORT_ID+1
        done
        echo $EXPORT_ID > $GANESHA_DIR/.export_added
        check_cmd_status `echo $?`
}

#This function removes an export dynamically(uses the export_id of the export)
function dynamic_export_remove()
{
        grep Export_Id $GANESHA_DIR/exports/export.$VOL.conf | \
        while read entry;
        do
                dbus-send --print-reply --system \
                --dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr \
                org.ganesha.nfsd.exportmgr.RemoveExport \
                uint16:$(echo $entry | awk -F"[=,;]" '{print$2}')
                check_cmd_status `echo $?`
        done
        sed -i /$VOL.conf/d $CONF
        rm -rf $GANESHA_DIR/exports/export.$VOL.conf

}

if [ "$OPTION" = "on" ];
then
        dynamic_export_add $@
fi

if [ "$OPTION" = "off" ];
then
        dynamic_export_remove $@
fi

