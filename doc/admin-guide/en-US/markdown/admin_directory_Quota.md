#Managing Directory Quota

Directory quotas in GlusterFS allows you to set limits on usage of the disk
space by directories or volumes. The storage administrators can control
the disk space utilization at the directory and/or volume levels in
GlusterFS by setting limits to allocatable disk space at any level in
the volume and directory hierarchy. This is particularly useful in cloud
deployments to facilitate utility billing model.

> **Note**
> For now, only Hard limits are supported. Here, the limit cannot be
> exceeded and attempts to use more disk space or inodes beyond the set
> limit is denied.

System administrators can also monitor the resource utilization to limit
the storage for the users depending on their role in the organization.

You can set the quota at the following levels:

-   **Directory level** – limits the usage at the directory level
-   **Volume level** – limits the usage at the volume level

> **Note**
> You can set the disk limit on the directory even if it is not created.
> The disk limit is enforced immediately after creating that directory.

##Enabling Quota

You must enable Quota to set disk limits.

**To enable quota:**

-   Use the following command to enable quota:

        # gluster volume quota  enable

    For example, to enable quota on the test-volume:

        # gluster volume quota test-volume enable
        Quota is enabled on /test-volume

##Disabling Quota

You can disable Quota, if needed.

**To disable quota:**

-   Use the following command to disable quota:

        # gluster volume quota  disable

    For example, to disable quota translator on the test-volume:

        # gluster volume quota test-volume disable
        Quota translator is disabled on /test-volume

##Setting or Replacing Disk Limit

You can create new directories in your storage environment and set the
disk limit or set disk limit for the existing directories. The directory
name should be relative to the volume with the export directory/mount
being treated as "/".

**To set or replace disk limit:**

-   Set the disk limit using the following command:

        # gluster volume quota  limit-usage /

    For example, to set limit on data directory on the test-volume where
    data is a directory under the export directory:

        # gluster volume quota test-volume limit-usage /data 10GB
        Usage limit has been set on /data

    > **Note**
    > In a multi-level directory hierarchy, the strictest disk limit
    > will be considered for enforcement.

##Displaying Disk Limit Information

You can display disk limit information on all the directories on which
the limit is set.

**To display disk limit information:**

-   Display disk limit information of all the directories on which limit
    is set, using the following command:

        # gluster volume quota  list

    For example, to see the set disks limit on the test-volume:

        # gluster volume quota test-volume list
        /Test/data    10 GB       6 GB
        /Test/data1   10 GB       4 GB

-   Display disk limit information on a particular directory on which
    limit is set, using the following command:

        # gluster volume quota  list

    For example, to view the set limit on /data directory of test-volume:

        # gluster volume quota test-volume list /data
        /Test/data    10 GB       6 GB

###Displaying Quota Limit Information Using the df Utility

You can create a report of the disk usage using the df utility by taking quota limits into consideration. To generate a report, run the following command:

        # gluster volume set VOLNAME quota-deem-statfs on

In this case, the total disk space of the directory is taken as the quota hard limit set on the directory of the volume.

>**Note**
>The default value for quota-deem-statfs is off. However, it is recommended to set quota-deem-statfs to on.

The following example displays the disk usage when quota-deem-statfs is off:

        # gluster volume set test-volume features.quota-deem-statfs off
        volume set: success
        # gluster volume quota test-volume list
        Path        Hard-limit    Soft-limit     Used     Available
        -----------------------------------------------------------
        /              300.0GB        90%        11.5GB     288.5GB
        /John/Downloads 77.0GB        75%        11.5GB     65.5GB

Disk usage for volume test-volume as seen on client1:

        # df -hT /home
        Filesystem           Type            Size  Used Avail Use% Mounted on
        server1:/test-volume fuse.glusterfs  400G   12G  389G   3% /home

The following example displays the disk usage when quota-deem-statfs is on:

        # gluster volume set test-volume features.quota-deem-statfs on
        volume set: success
        # gluster vol quota test-volume list
        Path        Hard-limit    Soft-limit     Used     Available
        -----------------------------------------------------------
        /              300.0GB        90%        11.5GB     288.5GB
        /John/Downloads 77.0GB        75%        11.5GB     65.5GB

Disk usage for volume test-volume as seen on client1:

        # df -hT /home
        Filesystem            Type            Size  Used Avail Use% Mounted on
        server1:/test-volume  fuse.glusterfs  300G   12G  289G   4% /home

The quota-deem-statfs option when set to on, allows the administrator to make the user view the total disk space available on the directory as the hard limit set on it.

##Updating Memory Cache Size

### Setting Timeout

For performance reasons, quota caches the directory sizes on client. You
can set timeout indicating the maximum valid duration of directory sizes
in cache, from the time they are populated.

For example: If there are multiple clients writing to a single
directory, there are chances that some other client might write till the
quota limit is exceeded. However, this new file-size may not get
reflected in the client till size entry in cache has become stale
because of timeout. If writes happen on this client during this
duration, they are allowed even though they would lead to exceeding of
quota-limits, since size in cache is not in sync with the actual size.
When timeout happens, the size in cache is updated from servers and will
be in sync and no further writes will be allowed. A timeout of zero will
force fetching of directory sizes from server for every operation that
modifies file data and will effectively disables directory size caching
on client side.

**To update the memory cache size:**

-   Use the following command to update the memory cache size:

        # gluster volume set  features.quota-timeout

    For example, to update the memory cache size for every 5 seconds on
    test-volume:

        # gluster volume set test-volume features.quota-timeout 5
        Set volume successful

##Setting Alert Time

Alert time is the frequency at which you want your usage information to be logged after you reach the soft limit.

**To set the alert time:**

-  Use the following command to set the alert time:

        # gluster volume quota VOLNAME alert-time time

    >**Note**
    >
    >The default alert-time is one week.

    For example, to set the alert time to one day:

        # gluster volume quota test-volume alert-time 1d
        volume quota : success

##Removing Disk Limit

You can remove set disk limit, if you do not want quota anymore.

**To remove disk limit:**

-   Use the following command to remove the disk limit set on a particular directory:

        # gluster volume quota  remove

    For example, to remove the disk limit on /data directory of
    test-volume:

        # gluster volume quota test-volume remove /data
        Usage limit set on /data is removed
