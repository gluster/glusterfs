Upgrade Steps For Quota
=======================

The upgrade process for quota involves executing two upgrade scripts:   
1. pre-upgrade-script-for-quota.sh, and   
2. post-upgrade-script-for-quota.sh

Pre-Upgrade Script:
==================

###What it does:

The pre-upgrade script (pre-upgrade-script-for-quota.sh) iterates over the list of volumes that have quota enabled and captures the configured quota limits for each such volume in a file under /var/tmp/glusterfs/quota-config-backup/vol_&lt;VOLNAME&gt; by executing 'quota list' command on each one of them.

###Pre-requisites for running Pre-Upgrade Script:

1. Make sure glusterd and the brick processes are running on all nodes in the cluster.
2. The pre-upgrade script must be run prior to upgradation.
3. The pre-upgrade script must be run on only one of the nodes in the cluster.

###Location:
pre-upgrade-script-for-quota.sh must be retrieved from the source tree under the 'extras' directory.

###Invocation:
Invoke the script by executing `./pre-upgrade-script-for-quota.sh` from the shell on any one of the nodes in the cluster.

* Example:   
  <code>
  [root@server1 extras]#./pre-upgrade-script-for-quota.sh
  </code>

Post-Upgrade Script:
===================

###What it does:
The post-upgrade script (post-upgrade-script-for-quota.sh)  picks the volumes that have quota enabled.

Because the cluster must be operating at op-version 3 for quota to work, the 'default-soft-limit' for each of these volumes is set to 80% (which is its default value) via `volume set` operation as an explicit trigger to bump up the op-version of the cluster and also to trigger a re-write of volfiles which knocks quota off client volume file.

Once this is done, these volumes are started forcefully using `volume start force` to launch the Quota Daemon on all the nodes.

Thereafter, for each of these volumes, the paths and the limits configured on them are retrieved from the backed up file /var/tmp/glusterfs/quota-config-backup/vol_&lt;VOLNAME&gt; and limits are set on them via the `quota limit-usage` interface.

####Note:
In the new version of quota, the command `quota limit-usage` will fail if the directory on which quota limit is to be set for a given volume does not exist. Therefore, it is advised that you create these directories first before running post-upgrade-script-for-quota.sh if you want limits to be set on these directories.

###Pre-requisites for running Post-Upgrade Script:
1. The post-upgrade script must be executed after all the nodes in the cluster have upgraded.
2. Also, all the clients accessing the given volume must also be upgraded before the script is run.
3. Make sure glusterd and the brick processes are running on all nodes in the cluster post upgrade.
4. The script must be run from the same node where the pre-upgrade script was run.


###Location:
post-upgrade-script-for-quota.sh can be found under the 'extras' directory of the source tree for glusterfs.

###Invocation:
post-upgrade-script-for-quota.sh takes one command line argument. This argument could be one of the following:
1. the name of the volume which has quota enabled; or
2. 'all'.

In the first case, invoke post-upgrade-script-for-quota.sh from the shell for each volume with quota enabled, with the name of the volume passed as an argument in the command-line:

* Example:   
  For a volume "vol1" on which quota is enabled, invoke the script in the following way:
  <code>
  [root@server1 extras]#./post-upgrade-script-for-quota.sh vol1
  </code>

In the second case, the post-upgrade script picks on its own, the volumes on which quota is enabled, and executes the post-upgrade procedure on each one of them. In this case, invoke post-upgrade-script-for-quota.sh from the shell with 'all' passed as an argument in the command-line:

* Example:   
  <code>
  [root@server1 extras]#./post-upgrade-script-for-quota.sh all
  </code>

####Note:
1. In the second case, post-upgrade-script-for-quota.sh exits prematurely upon failure to ugprade any given volume. In that case, you may run post-upgrade-script-for-quota.sh individually (using the volume name as command line argument) on this volume and also on all volumes appearing after this volume in the output of `gluster volume list`, that have quota enabled.
2. The backed up files under /var/tmp/glusterfs/quota-config-backup/ are retained after the post-upgrade procedure for reference.
