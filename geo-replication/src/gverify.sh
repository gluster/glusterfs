#!/bin/bash

# Script to verify the Primary and Secondary Gluster compatibility.
# To use ./gverify <primary volume> <secondary user> <secondary host> <secondary volume> <ssh port> <log file>
# Returns 0 if primary and secondary compatible.

# Considering buffer_size 100MB
BUFFER_SIZE=104857600;
SSH_PORT=$5;
primary_log_file=`gluster --print-logdir`/geo-replication/gverify-primarymnt.log
secondary_log_file=`gluster --print-logdir`/geo-replication/gverify-secondarymnt.log

function SSHM()
{
    if [[ -z "${GR_SSH_IDENTITY_KEY}" ]]; then
        ssh -p ${SSH_PORT} -q \
	    -oPasswordAuthentication=no \
	    -oStrictHostKeyChecking=no \
	    -oControlMaster=yes \
	    "$@";
    else
        ssh -p ${SSH_PORT} -i ${GR_SSH_IDENTITY_KEY} -q \
	    -oPasswordAuthentication=no \
	    -oStrictHostKeyChecking=no \
	    -oControlMaster=yes \
	    "$@";
    fi
}

function get_inode_num()
{
    local os
    case `uname -s` in
        NetBSD) os="NetBSD";;
        Linux)  os="Linux";;
        *)      os="Default";;
    esac

    if [[ "X$os" = "XNetBSD" ]]; then
        echo $(stat -f "%i" "$1")
    else
        echo $(stat -c "%i" "$1")
    fi
}

function umount_lazy()
{
    local os
    case `uname -s` in
        NetBSD) os="NetBSD";;
        Linux)  os="Linux";;
        *)      os="Default";;
    esac

    if [[ "X$os" = "XNetBSD" ]]; then
        umount -f -R "$1"
    else
        umount -l "$1"
    fi;
}

function disk_usage()
{
    local os
    case `uname -s` in
        NetBSD) os="NetBSD";;
        Linux)  os="Linux";;
        *)      os="Default";;
    esac

    if [[ "X$os" = "XNetBSD" ]]; then
        echo $(df -P "$1" | tail -1)
    else
        echo $(df -P -B1 "$1" | tail -1)
    fi;

}

function cmd_secondary()
{
    local cmd_line;
    cmd_line=$(cat <<EOF
function do_verify() {
ver=\$(gluster --version | head -1 | cut -f2 -d " ");
echo \$ver;
};
source /etc/profile && do_verify;
EOF
);

echo $cmd_line;
}

function primary_stats()
{
    PRIMARYVOL=$1;
    local inet6=$2;
    local d;
    local i;
    local disk_size;
    local used_size;
    local ver;
    local m_status;

    d=$(mktemp -d -t ${0##*/}.XXXXXX 2>/dev/null);
    if [ "$inet6" = "inet6" ]; then
        glusterfs -s localhost --xlator-option="*dht.lookup-unhashed=off" --xlator-option="transport.address-family=inet6" --volfile-id $PRIMARYVOL -l $primary_log_file $d;
    else
        glusterfs -s localhost --xlator-option="*dht.lookup-unhashed=off" --volfile-id $PRIMARYVOL -l $primary_log_file $d;
    fi

    i=$(get_inode_num $d);
    if [[ "$i" -ne "1" ]]; then
        echo 0:0;
        exit 1;
    fi;
    cd $d;
    disk_size=$(disk_usage $d | awk "{print \$2}");
    used_size=$(disk_usage $d | awk "{print \$3}");
    umount_lazy $d;
    rmdir $d;
    ver=$(gluster --version | head -1 | cut -f2 -d " ");
    m_status=$(echo "$disk_size:$used_size:$ver");
    echo $m_status
}


function secondary_stats()
{
set -x
    SECONDARYUSER=$1;
    SECONDARYHOST=$2;
    SECONDARYVOL=$3;
    local inet6=$4;
    local cmd_line;
    local ver;
    local status;

    d=$(mktemp -d -t ${0##*/}.XXXXXX 2>/dev/null);
    if [ "$inet6" = "inet6" ]; then
        glusterfs --xlator-option="*dht.lookup-unhashed=off" --xlator-option="transport.address-family=inet6" --volfile-server $SECONDARYHOST --volfile-id $SECONDARYVOL -l $secondary_log_file $d;
    else
        glusterfs --xlator-option="*dht.lookup-unhashed=off" --volfile-server $SECONDARYHOST --volfile-id $SECONDARYVOL -l $secondary_log_file $d;
    fi

    i=$(get_inode_num $d);
    if [[ "$i" -ne "1" ]]; then
        echo 0:0;
        exit 1;
    fi;
    cd $d;
    disk_size=$(disk_usage $d | awk "{print \$2}");
    used_size=$(disk_usage $d | awk "{print \$3}");
    no_of_files=$(find $d -maxdepth 1 -path "$d/.trashcan" -prune -o -path "$d" -o -print0 -quit);
    umount_lazy $d;
    rmdir $d;

    cmd_line=$(cmd_secondary);
    ver=`SSHM $SECONDARYUSER@$SECONDARYHOST bash -c "'$cmd_line'"`;
    status=$disk_size:$used_size:$ver:$no_of_files;
    echo $status
set +x
}

function ping_host ()
{
    ### Use bash internal socket support
    {
        exec 100<>/dev/tcp/$1/$2
        if [ $? -ne '0' ]; then
            return 1;
        else
            exec 100>&-
            return 0;
        fi
    } 1>&2 2>/dev/null
}

function main()
{
    log_file=$6
    > $log_file

    inet6=$7
    local cmd_line
    local ver

    # Use FORCE_BLOCKER flag in the error message to differentiate
    # between the errors which the force command should bypass

    # Test tcp connection to port 22, this is necessary since `ping`
    # does not work on all environments where 'ssh' is allowed but
    # ICMP is filterd

    ping_host $3 ${SSH_PORT}

    if [ $? -ne 0 ]; then
        echo "FORCE_BLOCKER|$3 not reachable." > $log_file
        exit 1;
    fi;

    if [[ -z "${GR_SSH_IDENTITY_KEY}" ]]; then
        ssh -p ${SSH_PORT} -oNumberOfPasswordPrompts=0 -oStrictHostKeyChecking=no $2@$3 "echo Testing_Passwordless_SSH";
    else
        ssh -p ${SSH_PORT} -i ${GR_SSH_IDENTITY_KEY} -oNumberOfPasswordPrompts=0 -oStrictHostKeyChecking=no $2@$3 "echo Testing_Passwordless_SSH";
    fi

    if [ $? -ne 0 ]; then
        echo "FORCE_BLOCKER|Passwordless ssh login has not been setup with $3 for user $2." > $log_file
        exit 1;
    fi;

    cmd_line=$(cmd_secondary);
    if [[ -z "${GR_SSH_IDENTITY_KEY}" ]]; then
        ver=$(ssh -p ${SSH_PORT} -oNumberOfPasswordPrompts=0 -oStrictHostKeyChecking=no $2@$3 bash -c "'$cmd_line'")
    else
        ver=$(ssh -p ${SSH_PORT} -i ${GR_SSH_IDENTITY_KEY} -oNumberOfPasswordPrompts=0 -oStrictHostKeyChecking=no $2@$3 bash -c "'$cmd_line'")
    fi

    if [ -z "$ver" ]; then
        echo "FORCE_BLOCKER|gluster command not found on $3 for user $2." > $log_file
        exit 1;
    fi;

    ERRORS=0;
    primary_data=$(primary_stats $1 ${inet6});
    secondary_data=$(secondary_stats $2 $3 $4 ${inet6});
    primary_disk_size=$(echo $primary_data | cut -f1 -d':');
    secondary_disk_size=$(echo $secondary_data | cut -f1 -d':');
    primary_used_size=$(echo $primary_data | cut -f2 -d':');
    secondary_used_size=$(echo $secondary_data | cut -f2 -d':');
    primary_version=$(echo $primary_data | cut -f3 -d':');
    secondary_version=$(echo $secondary_data | cut -f3 -d':');
    secondary_no_of_files=$(echo $secondary_data | cut -f4 -d':');

    if [[ "x$primary_disk_size" = "x" || "x$primary_version" = "x" || "$primary_disk_size" -eq "0" ]]; then
        echo "FORCE_BLOCKER|Unable to mount and fetch primary volume details. Please check the log: $primary_log_file" > $log_file;
	exit 1;
    fi;

    if [[ "x$secondary_disk_size" = "x" || "x$secondary_version" = "x" || "$secondary_disk_size" -eq "0" ]]; then
	echo "FORCE_BLOCKER|Unable to mount and fetch secondary volume details. Please check the log: $secondary_log_file" > $log_file;
	exit 1;
    fi;

    # The above checks are mandatory and force command should be blocked
    # if they fail. The checks below can be bypassed if force option is
    # provided hence no FORCE_BLOCKER flag.

    if [ "$secondary_disk_size" -lt "$primary_disk_size" ]; then
        echo "Total disk size of primary is greater than disk size of secondary." >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi

    effective_primary_used_size=$(( $primary_used_size + $BUFFER_SIZE ))
    secondary_available_size=$(( $secondary_disk_size - $secondary_used_size ))
    primary_available_size=$(( $primary_disk_size - $effective_primary_used_size ));

    if [ "$secondary_available_size" -lt "$primary_available_size" ]; then
        echo "Total available size of primary is greater than available size of secondary" >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi

    if [ ! -z $secondary_no_of_files ]; then
        echo "$3::$4 is not empty. Please delete existing files in $3::$4 and retry, or use force to continue without deleting the existing files." >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi;

    if [[ $primary_version != $secondary_version ]]; then
        echo "Gluster version mismatch between primary and secondary. Primary version: $primary_version Secondary version: $secondary_version" >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi;

    exit $ERRORS;
}


main "$@";
