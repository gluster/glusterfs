Command Reference
=================

This section describes the available commands and includes the following
section:

-   gluster Command

    Gluster Console Manager (command line interpreter)

-   glusterd Daemon

    Gluster elastic volume management daemon

gluster Command
===============

**NAME**

gluster - Gluster Console Manager (command line interpreter)

**SYNOPSIS**

To run the program and display the gluster prompt:

**gluster**

To specify a command directly: gluster [COMMANDS] [OPTIONS]

**DESCRIPTION**

The Gluster Console Manager is a command line utility for elastic volume
management. You can run the gluster command on any export server. The
command enables administrators to perform cloud operations such as
creating, expanding, shrinking, rebalancing, and migrating volumes
without needing to schedule server downtime.

**COMMANDS**

  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  Command                                                                                                    Description
  ---------------------------------------------------------------------------------------------------------- ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Volume**

  volume info [all | VOLNAME]                                                                                Displays information about all volumes, or the specified volume.

  volume create NEW-VOLNAME [stripe COUNT] [replica COUNT] [transport tcp | rdma | tcp,rdma] NEW-BRICK ...   Creates a new volume of the specified type using the specified bricks and transport type (the default transport type is tcp).

  volume delete VOLNAME                                                                                      Deletes the specified volume.

  volume start VOLNAME                                                                                       Starts the specified volume.

  volume stop VOLNAME [force]                                                                                Stops the specified volume.

  volume rename VOLNAME NEW-VOLNAME                                                                          Renames the specified volume.

  volume help                                                                                                Displays help for the volume command.

  **Brick**

  volume add-brick VOLNAME NEW-BRICK ...                                                                     Adds the specified brick to the specified volume.

  volume replace-brick VOLNAME (BRICK NEW-BRICK) start | pause | abort | status                              Replaces the specified brick.

  volume remove-brick VOLNAME [(replica COUNT)|(stripe COUNT)] BRICK ...                                     Removes the specified brick from the specified volume.

  **Rebalance**

  volume rebalance VOLNAME start                                                                             Starts rebalancing the specified volume.

  volume rebalance VOLNAME stop                                                                              Stops rebalancing the specified volume.

  volume rebalance VOLNAME status                                                                            Displays the rebalance status of the specified volume.

  **Log**

  volume log filename VOLNAME [BRICK] DIRECTORY                                                              Sets the log directory for the corresponding volume/brick.

  volume log rotate VOLNAME [BRICK]                                                                          Rotates the log file for corresponding volume/brick.

  volume log locate VOLNAME [BRICK]                                                                          Locates the log file for corresponding volume/brick.

  **Peer**

  peer probe HOSTNAME                                                                                        Probes the specified peer.

  peer detach HOSTNAME                                                                                       Detaches the specified peer.

  peer status                                                                                                Displays the status of peers.

  peer help                                                                                                  Displays help for the peer command.

  **Geo-replication**

  volume geo-replication MASTER SLAVE start                                                                  Start geo-replication between the hosts specified by MASTER and SLAVE. You can specify a local master volume as :VOLNAME.
                                                                                                             
                                                                                                             You can specify a local slave volume as :VOLUME and a local slave directory as /DIRECTORY/SUB-DIRECTORY. You can specify a remote slave volume as DOMAIN::VOLNAME and a remote slave directory as DOMAIN:/DIRECTORY/SUB-DIRECTORY.

  volume geo-replication MASTER SLAVE stop                                                                   Stop geo-replication between the hosts specified by MASTER and SLAVE. You can specify a local master volume as :VOLNAME and a local master directory as /DIRECTORY/SUB-DIRECTORY.
                                                                                                             
                                                                                                             You can specify a local slave volume as :VOLNAME and a local slave directory as /DIRECTORY/SUB-DIRECTORY. You can specify a remote slave volume as DOMAIN::VOLNAME and a remote slave directory as DOMAIN:/DIRECTORY/SUB-DIRECTORY.

  volume geo-replication MASTER SLAVE config [options]                                                                                                                                                                                                                                                                                             Configure geo-replication options between the hosts specified by MASTER and SLAVE.

  gluster-command COMMAND                                                                                    The path where the gluster command is installed.

  gluster-log-level LOGFILELEVEL                                                                             The log level for gluster processes.

  log-file LOGFILE                                                                                           The path to the geo-replication log file.

  log-level LOGFILELEVEL                                                                                     The log level for geo-replication.

  remote-gsyncd COMMAND                                                                                      The path where the gsyncd binary is installed on the remote machine.

  ssh-command COMMAND                                                                                        The ssh command to use to connect to the remote machine (the default is ssh).

  rsync-command COMMAND                                                                                      The rsync command to use for synchronizing the files (the default is rsync).

  volume\_id= UID                                                                                            The command to delete the existing master UID for the intermediate/slave node.

  timeout SECONDS                                                                                            The timeout period.

  sync-jobs N                                                                                                The number of simultaneous files/directories that can be synchronized.

                                                                                                             ignore-deletes                                                                                                                                                                                                                        If this option is set to 1, a file deleted on master will not trigger a delete operation on the slave. Hence, the slave will remain as a superset of the master and can be used to recover the master in case of crash and/or accidental delete.

  **Other**

  help                                                                                                                                                                                                                                                                                                                                             Display the command options.

  quit                                                                                                                                                                                                                                                                                                                                             Exit the gluster command line interface.
  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

**FILES**

/var/lib/glusterd/\*

**SEE ALSO**

fusermount(1), mount.glusterfs(8), glusterfs-volgen(8), glusterfs(8),
glusterd(8)

glusterd Daemon
===============

**NAME**

glusterd - Gluster elastic volume management daemon

**SYNOPSIS**

glusterd [OPTION...]

**DESCRIPTION**

The glusterd daemon is used for elastic volume management. The daemon
must be run on all export servers.

**OPTIONS**

  Option                              Description
  ----------------------------------- ----------------------------------------------------------------------------------------------------------------
  **Basic**
  -l=LOGFILE, --log-file=LOGFILE      Files to use for logging (the default is /usr/local/var/log/glusterfs/glusterfs.log).
  -L=LOGLEVEL, --log-level=LOGLEVEL   Logging severity. Valid options are TRACE, DEBUG, INFO, WARNING, ERROR and CRITICAL (the default is INFO).
  --debug                             Runs the program in debug mode. This option sets --no-daemon, --log-level to DEBUG, and --log-file to console.
  -N, --no-daemon                     Runs the program in the foreground.
  **Miscellaneous**
  -?, --help                          Displays this help.
  --usage                             Displays a short usage message.
  -V, --version                       Prints the program version.

**FILES**

/var/lib/glusterd/\*

**SEE ALSO**

fusermount(1), mount.glusterfs(8), glusterfs-volgen(8), glusterfs(8),
gluster(8)
