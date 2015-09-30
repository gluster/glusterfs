#WORM (Write Once Read Many)
This features enables you to create a `WORM volume` using gluster CLI.
##Description
WORM (write once,read many) is a desired feature for users who want to store data such as `log files` and where data is not allowed to get modified.

GlusterFS provides a new key `features.worm` which takes boolean values(enable/disable) for volume set.

Internally, the volume set command with 'feature.worm' key will add 'features/worm' translator in the brick's volume file.

`This change would be reflected on a subsequent restart of the volume`, i.e gluster volume stop, followed by a gluster volume start.

With a volume converted to WORM, the changes are as follows:

* Reads are handled normally
* Only files with O_APPEND flag will be supported.
* Truncation,deletion wont be supported.

##Volume Options
Use the volume set command on a volume and see if the volume is actually turned into WORM type.

    # features.worm enable
##Fully loaded Example
WORM feature is being supported from glusterfs version 3.4
start glusterd by using the command

    # service glusterd start
Now create a volume by using the command

    # gluster volume create <vol_name> <brick_path>
start the volume created by running the command below.

    # gluster vol start <vol_name>
Run the command below to make sure that volume is created.

    # gluster volume info
Now turn on the WORM feature on the volume by using the command

    # gluster vol set <vol_name> worm enable
Verify that the option is set by using the command

    # gluster volume info
User should be able to see another option in the volume info

    # features.worm: enable
Now restart the volume for the changes to reflect, by performing volume stop and start.

    # gluster volume <vol_name> stop
    # gluster volume <vol_name> start
Now mount the volume using fuse mount

    # mount -t glusterfs <vol_name> <mnt_point>
create a file inside the mount point by running the command below

    # touch <file_name>
Verify that user is able to create a file by running the command below

    # ls <file_name>

##How To Test
Now try deleting the above file which is been created

    # rm <file_name>
Since WORM is enabled on the volume, it gives the following error message `rm: cannot remove '/<mnt_point>/<file_name>': Read-only file system`

put some content into the file which is created above.

    # echo "at the end of the file" >> <file_name>
Now try editing the file by running the commnad below and verify that the following error message is displayed `rm: cannot remove '/<mnt_point>/<file_name>': Read-only file system`

    # sed -i "1iAt the beginning of the file" <file_name>
Now read the contents of the file and verify that file can be read.

    cat <file_name>

`Note: If WORM option is set on the volume before it is started, then volume need not be restarted for the changes to get reflected`.
