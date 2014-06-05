#!/bin/bash

# Script to verify the Master and Slave Gluster compatibility.
# To use ./gverify <master volume> <slave host> <slave volume>
# Returns 0 if master and slave compatible.

# Considering buffer_size 100MB
BUFFER_SIZE=104857600;
slave_log_file=`gluster --print-logdir`/geo-replication-slaves/slave.log

function SSHM()
{
    ssh -q \
	-oPasswordAuthentication=no \
	-oStrictHostKeyChecking=no \
	-oControlMaster=yes \
	"$@";
}

function cmd_master()
{
    VOL=$1;
    local cmd_line;
    cmd_line=$(cat <<EOF
function do_verify() {
v=\$1;
d=\$(mktemp -d 2>/dev/null);
glusterfs -s localhost --xlator-option="*dht.lookup-unhashed=off" --volfile-id \$v -l $slave_log_file \$d;
i=\$(stat -c "%i" \$d);
if [[ "\$i" -ne "1" ]]; then
echo 0:0;
exit 1;
fi;
cd \$d;
disk_size=\$(df -P -B1 \$d | tail -1 | awk "{print \\\$2}");
used_size=\$(df -P -B1 \$d | tail -1 | awk "{print \\\$3}");
umount -l \$d;
rmdir \$d;
ver=\$(gluster --version | head -1 | cut -f2 -d " ");
echo \$disk_size:\$used_size:\$ver;
};
cd /tmp;
[ x$VOL != x ] && do_verify $VOL;
EOF
);

echo $cmd_line;
}

function cmd_slave()
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

function master_stats()
{
    MASTERVOL=$1;
    local cmd_line;
    cmd_line=$(cmd_master $MASTERVOL);
    bash -c "$cmd_line";
}


function slave_stats()
{
    SLAVEUSER=$1;
    SLAVEHOST=$2;
    SLAVEVOL=$3;
    local cmd_line;
    local ver;
    local status;

    d=$(mktemp -d 2>/dev/null);
    glusterfs --xlator-option="*dht.lookup-unhashed=off" --volfile-server $SLAVEHOST --volfile-id $SLAVEVOL -l $slave_log_file $d;
    i=$(stat -c "%i" $d);
    if [[ "$i" -ne "1" ]]; then
        echo 0:0;
        exit 1;
    fi;
    cd $d;
    disk_size=$(df -P -B1 $d | tail -1 | awk "{print \$2}");
    used_size=$(df -P -B1 $d | tail -1 | awk "{print \$3}");
    no_of_files=$(find  $d -maxdepth 0 -empty);
    umount -l $d;
    rmdir $d;

    cmd_line=$(cmd_slave);
    ver=`SSHM $SLAVEUSER@$SLAVEHOST bash -c "'$cmd_line'"`;
    status=$disk_size:$used_size:$ver:$no_of_files;
    echo $status
}

function ping_host ()
{
    ### Use bash internal socket support
    {
        exec 400<>/dev/tcp/$1/$2
        if [ $? -ne '0' ]; then
            return 1;
        else
            exec 400>&-
            return 0;
        fi
    } 1>&2 2>/dev/null
}

function main()
{
    log_file=$5
    > $log_file

    SSH_PORT=22
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

    ssh -oNumberOfPasswordPrompts=0 $2@$3 "echo Testing_Passwordless_SSH";
    if [ $? -ne 0 ]; then
        echo "FORCE_BLOCKER|Passwordless ssh login has not been setup with $3 for user $2." > $log_file
        exit 1;
    fi;

    ERRORS=0;
    master_data=$(master_stats $1);
    slave_data=$(slave_stats $2 $3 $4);
    master_disk_size=$(echo $master_data | cut -f1 -d':');
    slave_disk_size=$(echo $slave_data | cut -f1 -d':');
    master_used_size=$(echo $master_data | cut -f2 -d':');
    slave_used_size=$(echo $slave_data | cut -f2 -d':');
    master_version=$(echo $master_data | cut -f3 -d':');
    slave_version=$(echo $slave_data | cut -f3 -d':');
    slave_no_of_files=$(echo $slave_data | cut -f4 -d':');

    if [[ "x$master_disk_size" = "x" || "x$master_version" = "x" || "$master_disk_size" -eq "0" ]]; then
        echo "FORCE_BLOCKER|Unable to fetch master volume details. Please check the master cluster and master volume." > $log_file;
	exit 1;
    fi;

    if [[ "x$slave_disk_size" = "x" || "x$slave_version" = "x" || "$slave_disk_size" -eq "0" ]]; then
	echo "FORCE_BLOCKER|Unable to fetch slave volume details. Please check the slave cluster and slave volume." > $log_file;
	exit 1;
    fi;

    # The above checks are mandatory and force command should be blocked
    # if they fail. The checks below can be bypassed if force option is
    # provided hence no FORCE_BLOCKER flag.

    if [ "$slave_disk_size" -lt "$master_disk_size" ]; then
        echo "Total disk size of master is greater than disk size of slave." >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi

    effective_master_used_size=$(( $master_used_size + $BUFFER_SIZE ))
    slave_available_size=$(( $slave_disk_size - $slave_used_size ))
    master_available_size=$(( $master_disk_size - $effective_master_used_size ));

    if [ "$slave_available_size" -lt "$master_available_size" ]; then
        echo "Total available size of master is greater than available size of slave" >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi

    if [ -z $slave_no_of_files ]; then
        echo "$3::$4 is not empty. Please delete existing files in $3::$4 and retry, or use force to continue without deleting the existing files." >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi;

    if [[ $master_version > $slave_version ]]; then
        echo "Gluster version mismatch between master and slave." >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi;

    exit $ERRORS;
}


main "$@";
