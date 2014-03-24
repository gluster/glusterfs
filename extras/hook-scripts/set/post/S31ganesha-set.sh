#!/bin/bash
PROGNAME="Sganesha-set"
OPTSPEC="volname:"
VOL=
declare -i EXPORT_ID
CONF1="/var/lib/ganesha/nfs-ganesha.conf"
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
        count=`ls -l /var/lib/ganesha/exports/*.conf | wc -l`
        if [ "$count" = "1" ] ;
                then
                EXPORT_ID=1
        else
        #if [ -s /var/lib/ganesha/export_removed ];
        #               then
        #               EXPORT_ID=`head -1 /var/lib/ganesha/export_removed`
        #               sed -i -e "1d" /var/lib/ganesha/export_removed
        #               else

                 EXPORT_ID=`cat /var/lib/ganesha/export_added`
                 EXPORT_ID=EXPORT_ID+1
        #fi
        fi
        echo $EXPORT_ID > /var/lib/ganesha/export_added
        sed -i s/Export_Id.*/"Export_Id = $EXPORT_ID;"/ \
/var/lib/ganesha/exports/export.$VOL.conf
        echo "%include \"/var/lib/ganesha/exports/export.$VOL.conf\"" >> $CONF1


}

function export_remove()
{
        $removed_id=`cat /var/lib/ganesha/exports/export.$VOL.conf | grep Export_Id | cut -d " " -f3`
        echo $removed_id >> /var/lib/ganesha/export_removed
}

function start_ganesha()
{
        if [ "$IS_HOST_SET" = "NO" ]
                then
                gluster volume set $VOL nfs-ganesha.enable OFF
        else
                check_gluster_nfs

                #Remove export entry from nfs-ganesha.conf
                sed -i /$VOL.conf/d  $CONF1
                pkill ganesha.nfsd
                sleep 10
                gluster volume set $VOL  nfs.disable ON
                sleep 4

                #Create a new export entry
                export_add
                if ls /usr/bin/ | grep -q "ganesha.nfsd"
                        then
                        sed -i s/FSAL_Shared.*/FSAL_Shared_Library=\
"\"\/usr\/lib64\/ganesha\/libfsalgluster.so\";"/ $CONF1
                        /usr/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_FULL_DEBUG -d
                        sleep 2
                else
                        sed -i s/FSAL_Shared.*/FSAL_Shared_Library=\
"\"\/usr\/local\/lib64\/ganesha\/libfsalgluster.so\";"/ $CONF1
                       /usr/local/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_FULL_DEBUG -d
                       sleep 2
                fi

                if ! ps aux | grep -q "[g]anesha.nfsd"
                        then
                                if [ "$gnfs" = "enabled" ]
                                        then
                                        gluster volume set $VOL nfs.disable OFF
                                fi
                         rm -rf /var/lib/ganesha/exports/*
                         rm -rf /var/lib/ganesha/export_added
                         gluster volume set $VOL nfs-ganesha.enable OFF
                         gluster volume set $VOL nfs-ganesha.host none
                         exit 1
                fi
         fi

}

#This function generates a new config file when ganesha.host is set
#If the volume is already exported, only hostname is changed
function set_hostname()
{
        if  ! ls /var/lib/ganesha/exports/  | grep -q $VOL.conf
                then
                write_conf $VOL $host_name >\
/var/lib/ganesha/exports/export.$VOL.conf
        else
                sed -i  s/hostname.*/"hostname=\
\"$host_name\";"/ /var/lib/ganesha/exports/export.$VOL.conf
        fi

}


function stop_ganesha()
{
        if  ps aux | grep -q  "[g]anesha.nfsd"
                then
                pkill ganesha.nfsd
                sleep 10
        fi
        gluster vol set $VOL nfs-ganesha.host none
        #Remove the specfic export configuration file
        rm -rf /var/lib/ganesha/exports/export.$VOL.conf
        #Remove that entry from nfs-ganesha.conf
        sed -i /$VOL.conf/d  $CONF1
        #If there are any other volumes exported, restart nfs-ganesha
        if [ "$(ls -A /var/lib/ganesha/exports)" ];
                then
                check_nfsd_loc
                $LOC/bin/ganesha.nfsd -f $CONF1 -L $LOG -N NIV_FULL_DEBUG -d
        else
                rm -rf /var/lib/ganesha/export_added
        fi

}

        parse_args $@
        if [ ! -d "/var/lib/ganesha/exports" ];
                then
                mkdir /var/lib/ganesha/exports
        fi
        if echo $enable_ganesha | grep -q -i "ON"
                then
                check_if_host_set $VOL
                start_ganesha
        elif echo $enable_ganesha | grep -q -i "OFF"
                then
                check_if_host_set
                if [ "$IS_HOST_SET" = "YES" ]
                        then
                        stop_ganesha
                fi
        fi
        if [ "$host_name" != "none" ];
                then
                check_if_host_set
                set_hostname
                         if  cat /var/lib/glusterd/vols/$VOL/info\
| grep -i -q  "nfs-ganesha.enable=on"
                                  then
                                  start_ganesha
                         fi
        fi


