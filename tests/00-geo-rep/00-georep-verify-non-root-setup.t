#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=900

### Basic Non-root geo-rep setup test with Distribute Replicate volumes

##Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd


##Variables
GEOREP_CLI="$CLI volume geo-replication"
master=$GMV0
SH0="127.0.0.1"
slave=${SH0}::${GSV0}
num_active=2
num_passive=2
master_mnt=$M0
slave_mnt=$M1

##User and group to be used for non-root geo-rep setup
usr="nroot"
grp="ggroup"

slave_url=$usr@$slave
slave_vol=$GSV0
ssh_url=$usr@$SH0

#Cleanup stale keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' /home/$usr/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' /home/$usr/.ssh/authorized_keys

############################################################
#SETUP VOLUMES AND VARIABLES

##create_and_start_master_volume
TEST $CLI volume create $GMV0 replica 2 $H0:$B0/${GMV0}{1,2,3,4};
TEST $CLI volume start $GMV0

##create_and_start_slave_volume
TEST $CLI volume create $GSV0 replica 2 $H0:$B0/${GSV0}{1,2,3,4};
TEST $CLI volume start $GSV0

##Mount master
#TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

##Mount slave
#TEST glusterfs -s $H0 --volfile-id $GSV0 $M1


##########################################################
#TEST FUNCTIONS

function distribute_key_non_root()
{
    ${GLUSTER_LIBEXECDIR}/set_geo_rep_pem_keys.sh $usr $master $slave_vol
    echo $?
}


function check_status_non_root()
{
    local search_key=$1
    $GEOREP_CLI $master $slave_url status | grep -F "$search_key" | wc -l
}


function check_and_clean_group()
{
        if [ $(getent group $grp) ]
        then
                groupdel $grp;
                echo $?
        else
                echo 0
        fi
}

function clean_lock_files()
{
        if [ ! -f /etc/passwd.lock ];
        then
                rm -rf /etc/passwd.lock;
        fi

        if [ ! -f /etc/group.lock ];
        then
                rm -rf /etc/group.lock;
        fi

        if [ ! -f /etc/shadow.lock ];
        then
                rm -rf /etc/shadow.lock;
        fi

        if [ ! -f /etc/gshadow.lock ];
        then
                rm -rf /etc/gshadow.lock;
        fi
}


###########################################################
#SETUP NON-ROOT GEO REPLICATION

##Create ggroup group
##First test if group exists and then create new one

EXPECT_WITHIN $GEO_REP_TIMEOUT 0 check_and_clean_group

##cleanup *.lock files

clean_lock_files

TEST /usr/sbin/groupadd $grp

clean_lock_files
##Del if exists and create non-root user and assign it to newly created group
userdel -r -f $usr
TEST /usr/sbin/useradd -G $grp $usr

##Modify password for non-root user to have control over distributing ssh-key
echo "$usr:pass" | chpasswd

##Set up mountbroker root
TEST gluster-mountbroker setup /var/mountbroker-root $grp

##Associate volume and non-root user to the mountbroker
TEST gluster-mountbroker add $slave_vol $usr

##Check ssh setting for clear text passwords
sed '/^PasswordAuthentication /{s/no/yes/}' -i /etc/ssh/sshd_config && grep '^PasswordAuthentication ' /etc/ssh/sshd_config && service sshd restart


##Restart glusterd to reflect mountbroker changages
TEST killall_gluster;
TEST glusterd;
TEST pidof glusterd;

##Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "3" brick_count ${META_VOL}
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

##Mount master
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "4" brick_count $GMV0
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

##Mount slave
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "4" brick_count $GSV0
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1

## Check status of mount-broker
TEST gluster-mountbroker status


##Setup password-less ssh for non-root user
#sshpass -p "pass" ssh-copy-id -i ~/.ssh/id_rsa.pub $ssh_url
##Run ssh agent
eval "$(ssh-agent -s)"
PASS="pass"


##Create a temp script to echo the SSH password, used by SSH_ASKPASS

SSH_ASKPASS_SCRIPT=/tmp/ssh-askpass-script
cat > ${SSH_ASKPASS_SCRIPT} <<EOL
#!/bin/bash
echo "${PASS}"
EOL
chmod u+x ${SSH_ASKPASS_SCRIPT}

##set no display, necessary for ssh to use with setsid and SSH_ASKPASS
export DISPLAY

export SSH_ASKPASS=${SSH_ASKPASS_SCRIPT}

DISPLAY=: setsid ssh-copy-id -o 'PreferredAuthentications=password' -o 'StrictHostKeyChecking=no' -i ~/.ssh/id_rsa.pub $ssh_url

##Setting up PATH for gluster binaries in case of source installation
##ssh -oNumberOfPasswordPrompts=0 -oStrictHostKeyChecking=no $ssh_url "echo "export PATH=$PATH:/usr/local/sbin" >> ~/.bashrc"

##Creating secret pem pub file
TEST gluster-georep-sshkey generate

##Create geo-rep non-root setup

TEST $GEOREP_CLI $master $slave_url create push-pem

#check for session creation
EXPECT_WITHIN $GEO_REP_TIMEOUT 4 check_status_non_root "Created"
#Config gluster-command-dir
TEST $GEOREP_CLI $master $slave_url config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $master $slave_url config slave-gluster-command-dir ${GLUSTER_CMD_DIR}

## Test for key distribution

EXPECT_WITHIN $GEO_REP_TIMEOUT  0 distribute_key_non_root

##Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Enable_metavolume
TEST $GEOREP_CLI $master $slave config use_meta_volume true

#Start_georep
TEST $GEOREP_CLI $master $slave_url start

## Meta volume is enabled so looking for 2 Active and 2 Passive sessions

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_non_root "Active"

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_non_root "Passive"

#Pause geo-replication session
TEST $GEOREP_CLI  $master $slave_url pause

#Resume geo-replication session
TEST $GEOREP_CLI  $master $slave_url resume

#Validate failure of volume stop when geo-rep is running
TEST ! $CLI volume stop $GMV0

#Negative test for ssh-port
#Port should be integer and between 1-65535 range

TEST ! $GEOREP_CLI $master $slave_url config ssh-port -22

TEST ! $GEOREP_CLI $master $slave_url config ssh-port abc

TEST ! $GEOREP_CLI $master $slave_url config ssh-port 6875943

TEST ! $GEOREP_CLI $master $slave_url config ssh-port 4.5

TEST ! $GEOREP_CLI $master $slave_url config ssh-port 22a

#Config Set ssh-port to validate int validation
TEST $GEOREP_CLI $master $slave config ssh-port 22

#Hybrid directory rename test BZ#1763439

TEST $GEOREP_CLI $master $slave_url config change_detector xsync
#verify master and slave mount

EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT "^1$" check_mounted ${master_mnt}
EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT "^1$" check_mounted ${slave_mnt}

#Create test data for hybrid crawl
TEST mkdir ${master_mnt}/dir1
TEST mkdir ${master_mnt}/dir1/dir2
TEST mkdir ${master_mnt}/dir1/dir3
TEST mkdir ${master_mnt}/hybrid_d1

mv ${master_mnt}/hybrid_d1 ${master_mnt}/hybrid_rn_d1
mv ${master_mnt}/dir1/dir2 ${master_mnt}/rn_dir2
mv ${master_mnt}/dir1/dir3 ${master_mnt}/dir1/rn_dir3

#Verify hybrid crawl data on slave
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/dir1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/hybrid_rn_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/rn_dir2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/dir1/rn_dir3

#Stop Geo-rep
TEST $GEOREP_CLI $master $slave_url stop

#Delete Geo-rep
TEST $GEOREP_CLI $master $slave_url delete

#Cleanup authorized_keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' /home/$usr/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' /home/$usr/.ssh/authorized_keys

#clear mountbroker
gluster-mountbroker remove --user $usr
gluster-mountbroker remove --volume $slave_vol

#delete group and user created for non-root setup
TEST userdel -r -f $usr
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 check_and_clean_group

##password script cleanup
rm -rf /tmp/ssh-askpass-script


cleanup;

