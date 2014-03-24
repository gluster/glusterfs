#/bin/bash
PROGNAME="Sganesha-reset"
OPTSPEC="volname:"
VOL=

function parse_args () {
        ARGS=$(getopt -l $OPTSPEC  -o "o" -name $PROGNAME $@)
        eval set -- "$ARGS"
        case $1 in
            --volname)
               shift
               VOL=$1
                ;;
        esac
}

function is_volume_started () {
        volname=$1
        echo "$(grep status /var/lib/glusterd/vols/"$volname"/info |\
                cut -d"=" -f2)"
}

parse_args $@
if ps aux | grep -q "[g]anesha.nfsd"
        then
        kill -s TERM `cat /var/run/ganesha.pid`
        sleep 10
        rm -rf /var/lib/ganesha/exports
        rm -rf /var/lib/ganesha/export_added
        sed -i /conf/d /var/lib/ganesha/nfs-ganesha.conf
        if [ "1" = $(is_volume_started "$VOL") ];
                then
                gluster volume start $VOL force
        fi
fi



