#/bin/bash

declare -i EXPORT_ID
GANESHA_DIR=${1%/}
OPTION=$2
VOL=$3
cfgline=$(grep ^CONFFILE= /etc/sysconfig/ganesha)
eval $(echo ${cfgline} | grep -F CONFFILE=)
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
        if [ "$count" = "0" ] ;
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
                 EXPORT_ID=EXPORT_ID+1
        #fi
        fi
        echo $EXPORT_ID > $GANESHA_DIR/.export_added
        check_cmd_status `echo $?`
        sed -i s/Export_Id.*/"Export_Id= $EXPORT_ID ;"/ \
$GANESHA_DIR/exports/export.$VOL.conf
        check_cmd_status `echo $?`
        dbus-send  --system \
--dest=org.ganesha.nfsd  /org/ganesha/nfsd/ExportMgr \
org.ganesha.nfsd.exportmgr.AddExport  string:$GANESHA_DIR/exports/export.$VOL.conf \
string:"EXPORT(Path=/$VOL)"
}

#This function removes an export dynamically(uses the export_id of the export)
function dynamic_export_remove()
{
        removed_id=`cat $GANESHA_DIR/exports/export.$VOL.conf |\
grep Export_Id | cut -d " " -f8`
        check_cmd_status `echo $?`
        dbus-send --print-reply --system \
--dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr \
org.ganesha.nfsd.exportmgr.RemoveExport uint16:$removed_id
        check_cmd_status `echo $?`
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

