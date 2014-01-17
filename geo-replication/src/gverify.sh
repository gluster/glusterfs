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
available_size=\$(df -B1 \$d | tail -1 | awk "{print \\\$2}");
umount -l \$d;
rmdir \$d;
ver=\$(gluster --version | head -1 | cut -f2 -d " ");
echo \$available_size:\$ver;
};
cd /tmp;
[ x$VOL != x ] && do_verify $VOL;
EOF
);

echo $cmd_line;
}

function cmd_slave()
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
available_size=\$(df -B1 \$d | tail -1 | awk "{print \\\$4}");
no_of_files=\$(find  \$d -maxdepth 0 -empty);
umount -l \$d;
rmdir \$d;
ver=\$(gluster --version | head -1 | cut -f2 -d " ");
echo \$available_size:\$ver:\$no_of_files:;
};
cd /tmp;
[ x$VOL != x ] && do_verify $VOL;
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
    SLAVEHOST=$1;
    SLAVEVOL=$2;
    local cmd_line;
    cmd_line=$(cmd_slave $SLAVEVOL);
    SSHM $SLAVEHOST bash -c "'$cmd_line'";
}


function main()
{
    log_file=$4
    > $log_file

    # Use FORCE_BLOCKER flag in the error message to differentiate
    # between the errors which the force command should bypass

    ping -w 5 $2;
    if [ $? -ne 0 ]; then
        echo "FORCE_BLOCKER|$2 not reachable." > $log_file
        exit 1;
    fi;

    ssh -oNumberOfPasswordPrompts=0 $2 "echo Testing_Passwordless_SSH";
    if [ $? -ne 0 ]; then
        echo "FORCE_BLOCKER|Passwordless ssh login has not been setup with $2." > $log_file
        exit 1;
    fi;

    ERRORS=0;
    master_data=$(master_stats $1);
    slave_data=$(slave_stats $2 $3);
    master_size=$(echo $master_data | cut -f1 -d':');
    slave_size=$(echo $slave_data | cut -f1 -d':');
    master_version=$(echo $master_data | cut -f2 -d':');
    slave_version=$(echo $slave_data | cut -f2 -d':');
    slave_no_of_files=$(echo $slave_data | cut -f3 -d':');

    if [[ "x$master_size" = "x" || "x$master_version" = "x" || "$master_size" -eq "0" ]]; then
	echo "FORCE_BLOCKER|Unable to fetch master volume details. Please check the master cluster and master volume." > $log_file;
	exit 1;
    fi;

    if [[ "x$slave_size" = "x" || "x$slave_version" = "x" || "$slave_size" -eq "0" ]]; then
	echo "FORCE_BLOCKER|Unable to fetch slave volume details. Please check the slave cluster and slave volume." > $log_file;
	exit 1;
    fi;

    # The above checks are mandatory and force command should be blocked
    # if they fail. The checks below can be bypassed if force option is
    # provided hence no FORCE_BLOCKER flag.

    if [ ! $slave_size -ge $(($master_size - $BUFFER_SIZE )) ]; then
	echo "Total size of master is greater than available size of slave." >> $log_file;
	ERRORS=$(($ERRORS + 1));
    fi;

    if [ -z $slave_no_of_files ]; then
        echo "$2::$3 is not empty. Please delete existing files in $2::$3 and retry, or use force to continue without deleting the existing files." >> $log_file;
        ERRORS=$(($ERRORS + 1));
    fi;
 
    if [[ $master_version > $slave_version ]]; then
	echo "Gluster version mismatch between master and slave." >> $log_file;
	ERRORS=$(($ERRORS + 1));
    fi;

    exit $ERRORS;
}


main "$@";
