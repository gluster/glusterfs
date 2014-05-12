#!/bin/bash
PROGNAME="Sganesha-set"
OPTSPEC="volname:"
VOL=
declare -i EXPORT_ID
GANESHA_DIR="/var/lib/glusterfs-ganesha"
CONF1="$GANESHA_DIR/nfs-ganesha.conf"
LOG="/tmp/ganesha.log"
gnfs="enabled"
enable_ganesha=""
host_name="none"
IS_HOST_SET="NO"
LOC=""



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
        if cat /var/lib/glusterd/vols/$VOL/info | grep -q "nfs-ganesha.host"
                then
                IS_HOST_SET="YES"
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
        if cat /var/lib/glusterd/vols/$VOL/info | grep -q "nfs.disable=ON"
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
        echo "Pseudo=\"/$1\";"
        echo "NFS_Protocols = \"3,4\" ;"
        echo "Transport_Protocols = \"UDP,TCP\" ;"
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
        dbus-send  --system \
--dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr \
org.ganesha.nfsd.exportmgr.RemoveExport int32:$removed_id

}

#This function adds a new export dynamically by sending dbus signals
function dynamic_export_add()
{
        dbus-send  --system --dest=org.ganesha.nfsd \
/org/ganesha/nfsd/ExportMgr org.ganesha.nfsd.exportmgr.AddExport \
string:$GANESHA_DIR/exports/export.$VOL.conf
        echo $?
}

function start_ganesha()
{
        if [ "$IS_HOST_SET" = "YES" ]
                then
                check_gluster_nfs
                #Remove export entry from nfs-ganesha.conf
                sed -i /$VOL.conf/d  $CONF1
                sleep 4
                #Create a new export entry
                export_add
                if ! ps aux | grep -q  "[g]anesha.nfsd"
                        then
                        if ls /usr/bin/ | grep -q "ganesha.nfsd"
                                then
                                /usr/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_FULL_DEBUG -d
                                sleep 2
                        else
                                /usr/local/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_FULL_DEBUG -d
                                sleep 2
                        fi
                else
                        ret=$(dynamic_export_add $VOL)
                fi


              if  !(  ps aux | grep -q "[g]anesha.nfsd") || [ "$ret" == "1" ]
                        then
                         rm -rf $GANESHA_DIR/exports/*
                         rm -rf $GANESHA_DIR/.export_added
                         exit 1
                fi
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
                fi
                cp /etc/glusterfs-ganesha/nfs-ganesha.conf $GANESHA_DIR/
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
        if [ ! -d "$GANESHA_DIR/exports" ];
                then
                mkdir $GANESHA_DIR/exports
        fi
        if echo $enable_ganesha | grep -q -i "ON"
                then
                check_if_host_set $VOL
                start_ganesha
        elif echo $enable_ganesha | grep -q -i "OFF"
                then
                check_if_host_set $VOL
                if [ "$IS_HOST_SET" = "YES" ]
                        then
                        stop_ganesha
                        exit 0
                fi
        fi
        if [ "$host_name" != "none" ];
                then
                check_if_host_set $VOL
                set_hostname
                         if  cat /var/lib/glusterd/vols/$VOL/info\
| grep -i -q  "nfs-ganesha.enable=on"
                                  then
                                  dynamic_export_remove $VOL
                                  rm -rf $GANESHA_DIR/exports/export.$VOL.conf
                                  set_hostname
                                  start_ganesha
                         fi
        fi


