#!/bin/bash
PROGNAME="Sganesha-start"
OPTSPEC="volname:,gd-workdir:"
VOL=
declare -i EXPORT_ID
ganesha_key="ganesha.enable"
GANESHA_DIR="/etc/ganesha"
CONF1="$GANESHA_DIR/ganesha.conf"
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
                    break
                    ;;
            esac
            shift
        done
}



#This function generates a new export entry as export.volume_name.conf
function write_conf()
{
echo -e "# WARNING : Using Gluster CLI will overwrite manual
# changes made to this file. To avoid it, edit the
# file, copy it over to all the NFS-Ganesha nodes
# and run ganesha-ha.sh --refresh-config."

echo "EXPORT{"
echo "      Export_Id = 2;"
echo "      Path = \"/$VOL\";"
echo "      FSAL {"
echo "           name = \"GLUSTER\";"
echo "           hostname=\"localhost\";"
echo "           volume=\"$VOL\";"
echo "           }"
echo "      Access_type = RW;"
echo "      Disable_ACL = true;"
echo "      Squash=\"No_root_squash\";"
echo "      Pseudo=\"/$VOL\";"
echo "      Protocols = \"3\", \"4\" ;"
echo "      Transports = \"UDP\",\"TCP\";"
echo "      SecType = \"sys\";"
echo "}"
}

#This function keeps track of export IDs and increments it with every new entry
function export_add()
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
        #fi
        fi
        for entry in `grep -n Export_Id  $GANESHA_DIR/exports/export.$VOL.conf \
        | awk -F":" '{print$1}'`
        do
                sed -e "$entry s/Export_Id.*/Export_Id=$EXPORT_ID ;/" -i \
                $GANESHA_DIR/exports/export.$VOL.conf
                dbus-send  --system \
                --dest=org.ganesha.nfsd  /org/ganesha/nfsd/ExportMgr \
                org.ganesha.nfsd.exportmgr.AddExport  \
                string:$GANESHA_DIR/exports/export.$VOL.conf \
                string:"EXPORT(Export_Id=$EXPORT_ID)"
                EXPORT_ID=EXPORT_ID+1
        done
        echo $EXPORT_ID > $GANESHA_DIR/.export_added
        echo "%include \"$GANESHA_DIR/exports/export.$VOL.conf\"" >> $CONF1
}

function start_ganesha()
{
        #Remove export entry from nfs-ganesha.conf
        sed -i /$VOL.conf/d  $CONF1
        #Create a new export entry
        export_add $VOL
}

# based on src/scripts/ganeshactl/Ganesha/export_mgr.py
function is_exported()
{
        local volume="${1}"

        dbus-send --type=method_call --print-reply --system \
                  --dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr \
                  org.ganesha.nfsd.exportmgr.ShowExports \
            | grep -w -q "/${volume}"

        return $?
}

# Check the info file (contains the volume options) to see if Ganesha is
# enabled for this volume.
function ganesha_enabled()
{
        local volume="${1}"
        local info_file="${GLUSTERD_WORKDIR}/vols/${VOL}/info"
        local enabled="off"

        enabled=$(grep -w ${ganesha_key} ${info_file} | cut -d"=" -f2)

        [ "${enabled}" == "on" ]

        return $?
}

parse_args $@

if ganesha_enabled ${VOL} && ! is_exported ${VOL}
then
        if [ ! -e ${GANESHA_DIR}/exports/export.${VOL}.conf ]
        then
                write_conf ${VOL} > ${GANESHA_DIR}/exports/export.${VOL}.conf
        fi
        start_ganesha ${VOL}
fi

exit 0
