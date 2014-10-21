#/bin/bash
PROGNAME="Sganesha-reset"
OPTSPEC="volname:,gd-workdir:"
VOL=
GLUSTERD_WORKDIR=

function parse_args () {
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

function is_volume_started () {
        volname=$1
        echo "$(grep status $GLUSTERD_WORKDIR/vols/"$volname"/info |\
                cut -d"=" -f2)"
}

parse_args $@
if ps aux | grep -q "[g]anesha.nfsd"
        then
        kill -s TERM `cat /var/run/ganesha.pid`
        sleep 10
        rm -rf /var/lib/glusterfs-ganesha/exports
        rm -rf /var/lib/glusterfs-ganesha/.export_added
        sed -i /conf/d /var/lib/ganesha/nfs-ganesha.conf
        if [ "1" = $(is_volume_started "$VOL") ];
                then
                gluster volume start $VOL force
        fi
fi
