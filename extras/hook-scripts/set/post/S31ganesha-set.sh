#!/bin/bash
PROGNAME="Sganesha-set"
OPTSPEC="volname:,gd-workdir:"
VOL=
declare -i EXPORT_ID
GANESHA_DIR="/var/lib/glusterfs-ganesha"
CONF1="$GANESHA_DIR/nfs-ganesha.conf"
GANESHA_LOG_DIR="/var/log/nfs-ganesha/"
LOG="$GANESHA_LOG_DIR/ganesha.nfsd.log"
gnfs="enabled"
enable_ganesha=""
host_name="none"
LOC=""
GLUSTERD_WORKDIR=


function parse_args ()
{
        ARGS=$(getopt -l $OPTSPEC  -o "o" -name $PROGNAME $@)
        eval set -- "$ARGS"

        while true; do
            case $1 in
                --volname)
                    shift
                    VOL=$1
                    ;;
                --gd-workdir)
                    shift
                    GLUSTERD_WORKDIR=$1
                    ;;
                *)
                    shift
                    for pair in $@; do
                        read key value < <(echo "$pair" | tr "=" " ")
                        case "$key" in
                            "nfs-ganesha.enable")
                                enable_ganesha=$value
                                ;;
                            "nfs-ganesha.host")
                                host_name=$value
                                ;;
                            *)
                                ;;
                        esac
                    done
                    shift
                    break
                    ;;
            esac
            shift
        done
}


function check_if_host_set()
{
        if ! cat $GLUSTERD_WORKDIR/vols/$VOL/info | grep -q "nfs-ganesha.host"
                then
                exit 1
        fi
}

function check_nfsd_loc()
{
        if ls /usr/bin | grep "[g]anesha.nfsd"
                then
                LOC="/usr"
        else
                LOC="/usr/local"
        fi
}


function check_gluster_nfs()
{
        if cat $GLUSTERD_WORKDIR/vols/$VOL/info | grep -q "nfs.disable=ON"
                 then
                 gnfs="disabled"
        fi
}

function check_cmd_status()
{
        if [ "$1" != "0" ]
                 then
                 rm -rf $GANESHA_DIR/exports/export.$VOL.conf
                 exit 1
        fi
}



#This function generates a new export entry as export.volume_name.conf
function write_conf()
{
        echo "EXPORT{
        "
        echo "Export_Id = ;"
        echo "Path=\"/$1\";"
        echo "FSAL {
        "
        echo "name = "GLUSTER";"
        echo "hostname=\"$2\";"
        echo  "volume=\"$1\";"
        echo "}"
        echo "Access_type = RW;"
        echo "Squash = No_root_squash;"
        echo "Disable_ACL = TRUE;"
        echo "Pseudo=\"/$1\";"
        echo "Protocols = \"3,4\" ;"
        echo "Transports = \"UDP,TCP\" ;"
        echo "SecType = \"sys\";"
        echo "Tag = \"$1\";"
        echo "}"
}

#This function keeps track of export IDs and increments it with every new entry
function export_add()
{
        count=`ls -l $GANESHA_DIR/exports/*.conf | wc -l`
        if [ "$count" = "1" ] ;
                then
                EXPORT_ID=1
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
        sed -i s/Export_Id.*/"Export_Id = $EXPORT_ID ;"/ \
$GANESHA_DIR/exports/export.$VOL.conf
        echo "%include \"$GANESHA_DIR/exports/export.$VOL.conf\"" >> $CONF1
        check_cmd_status `echo $?`
}

#This function removes an export dynamically(uses the export_id of the export)
function dynamic_export_remove()
{
        removed_id=`cat $GANESHA_DIR/exports/export.$VOL.conf |\
grep Export_Id | cut -d " " -f3`
        check_cmd_status `echo $?`
        dbus-send --print-reply --system \
--dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr \
org.ganesha.nfsd.exportmgr.RemoveExport int32:$removed_id
        check_cmd_status `echo $?`

}

#This function adds a new export dynamically by sending dbus signals
function dynamic_export_add()
{
        dbus-send --print-reply --system --dest=org.ganesha.nfsd \
/org/ganesha/nfsd/ExportMgr org.ganesha.nfsd.exportmgr.AddExport \
string:$GANESHA_DIR/exports/export.$VOL.conf string:"EXPORT(Tag=$VOL)"
        check_cmd_status `echo $?`

}

function start_ganesha()
{
        check_gluster_nfs
        #Remove export entry from nfs-ganesha.conf
        sed -i /$VOL.conf/d  $CONF1
        #Create a new export entry
        export_add
        if ! ps aux | grep -q  "[g]anesha.nfsd"
                then
                if ls /usr/bin/ganesha.nfsd
                       then
                       /usr/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_EVENT -d
                        sleep 2
                else
                        /usr/local/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_EVENT -d
                        sleep 2
                fi
        else
                        dynamic_export_add $VOL
        fi


        if  !(  ps aux | grep -q "[g]anesha.nfsd")
                then
                rm -rf $GANESHA_DIR/exports/*
                rm -rf $GANESHA_DIR/.export_added
                exit 1
        fi

}

#This function generates a new config file when ganesha.host is set
#If the volume is already exported, only hostname is changed
function set_hostname()
{
        if  ! ls $GANESHA_DIR/exports/  | grep -q $VOL.conf
                then
                write_conf $VOL $host_name >\
$GANESHA_DIR/exports/export.$VOL.conf
        else
                sed -i  s/hostname.*/"hostname=\
\"$host_name\";"/ $GANESHA_DIR/exports/export.$VOL.conf
        fi
}


function check_ganesha_dir()
{
        #Check if the configuration file is placed in /etc/glusterfs-ganesha
        if ! ls  /etc/glusterfs-ganesha  | grep "nfs-ganesha.conf"
        then
               exit 1
        else
                if [ ! -d "$GANESHA_DIR" ];
                         then
                         mkdir $GANESHA_DIR
                         check_cmd_status `echo $?`
                fi
                cp /etc/glusterfs-ganesha/nfs-ganesha.conf $GANESHA_DIR/
                check_cmd_status `echo $?`
        fi
        if [ ! -d "$GANESHA_DIR/exports" ];
                then
                mkdir $GANESHA_DIR/exports
                check_cmd_status `echo $?`
        fi
        if [ ! -d "$GANESHA_LOG_DIR" ] ;
                then
                mkdir $GANESHA_LOG_DIR
                check_cmd_status `echo $?`
        fi



}

function stop_ganesha()
{
        dynamic_export_remove $VOL
        #Remove the specfic export configuration file
        rm -rf $GANESHA_DIR/exports/export.$VOL.conf
        #Remove that entry from nfs-ganesha.conf
        sed -i /$VOL.conf/d  $CONF1
        #If there are no other volumes exported, stop nfs-ganesha
        if [ ! "$(ls -A $GANESHA_DIR/exports)" ];
                then
                pkill ganesha.nfsd
                rm -rf $GANESHA_DIR/.export_added
        fi
}

        parse_args $@
        check_ganesha_dir $VOL
        if echo $enable_ganesha | grep -q -i "ON"
                then
                check_if_host_set $VOL
                if ! showmount -e localhost | cut -d "" -f1 | grep -q "$VOL[[:space:]]"
                        then
                        start_ganesha
                fi
        elif echo $enable_ganesha | grep -q -i "OFF"
                then
                check_if_host_set $VOL
                stop_ganesha
        fi
        if [ "$host_name" != "none" ];
                then
                if showmount -e localhost | cut -d "" -f1 | grep -q "$VOL[[:space:]]"
                        then
                        dynamic_export_remove $VOL
                        set_hostname
                        start_ganesha
                else
                        set_hostname
                fi

        fi
