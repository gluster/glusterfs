#/bin/bash

#This script is called by glusterd when the user
#tries to export a volume via NFS-Ganesha.
#An export file specific to a volume
#is created in GANESHA_DIR/exports.

GANESHA_DIR=$1
VOL=$2

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

CONF=$(cat /etc/sysconfig/ganesha | grep "CONFFILE" | cut -f 2 -d "=")
check_cmd_status `echo $?`


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
echo "           name = "GLUSTER";"
echo "           hostname=\"localhost\";"
echo  "          volume=\"$VOL\";"
echo "           }"
echo "      Access_type = RW;"
echo '      Squash="No_root_squash";'
echo "      Pseudo=\"/$VOL\";"
echo '      Protocols = "3", "4" ;'
echo '      Transports = "UDP","TCP";'
echo '      SecType = "sys";'
echo "     }"
}

write_conf $@ > $GANESHA_DIR/exports/export.$VOL.conf
if ! (cat $CONF | grep  $VOL.conf$ )
then
echo "%include \"$GANESHA_DIR/exports/export.$VOL.conf\"" >> $CONF
fi
