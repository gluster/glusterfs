#File Snapshot
This feature gives the ability to take snapshot of files.

##Descritpion
This feature adds file snapshotting support to glusterfs. Snapshots can be created , deleted and reverted.

To take a snapshot of a file, file should be in QCOW2 format as the code for the block layer snapshot has been taken from Qemu and put into gluster as a translator.

With this feature, glusterfs will have better integration with Openstack Cinder, and in general ability to take snapshots of files (typically VM images).

New extended attribute (xattr) will be added to identify files which are 'snapshot managed' vs raw files.

##Volume Options
Following volume option needs to be set on the volume for taking file snapshot.

    # features.file-snapshot on
##CLI parameters
Following cli parameters needs to be passed with setfattr command to create, delete and revert file snapshot.

    # trusted.glusterfs.block-format
    # trusted.glusterfs.block-snapshot-create
    # trusted.glusterfs.block-snapshot-goto
##Fully loaded Example
Download glusterfs3.5 rpms from download.gluster.org
Install these rpms.

start glusterd by using the command

    # service glusterd start
Now create a volume by using the command

    # gluster volume create <vol_name> <brick_path>
Run the command below to make sure that volume is created.

    # gluster volume info
Now turn on the snapshot feature on the volume by using the command

    # gluster volume set <vol_name> features.file-snapshot on
Verify that the option is set by using the command

    # gluster volume info
User should be able to see another option in the volume info

    # features.file-snapshot: on
Now mount the volume using fuse mount

    # mount -t glusterfs <vol_name> <mount point>
cd into the mount point
    # cd <mount_point>
    # touch <file_name>
Size of the file can be set and format of the file can be changed to QCOW2 by running the command below. File size can be in KB/MB/GB

    # setfattr -n trusted.glusterfs.block-format -v qcow2:<file_size> <file_name>
Now create another file and send data to that file by running the command

    # echo 'ABCDEFGHIJ' > <data_file1>
copy the data to the one file to another by running the command

    # dd if=data-file1 of=big-file conv=notrunc
Now take the `snapshot of the file` by running the command

    # setfattr -n trusted.glusterfs.block-snapshot-create -v <image1> <file_name>
Add some more contents to the file and take another file snaphot by doing the following steps

    # echo '1234567890' > <data_file2>
    # dd if=<data_file2> of=<file_name> conv=notrunc
    # setfattr -n trusted.glusterfs.block-snapshot-create -v <image2> <file_name>
Now `revert` both the file snapshots and write data to some files so that data can be compared.

    # setfattr -n trusted.glusterfs.block-snapshot-goto -v <image1> <file_name>
    # dd if=<file_name> of=<out-file1> bs=11 count=1
    # setfattr -n trusted.glusterfs.block-snapshot-goto -v <image2> <file_name>
    # dd if=<file_name> of=<out-file2> bs=11 count=1
Now read the contents of the files and compare as below:

    # cat <data_file1>, <out_file1>  and compare contents.
    # cat <data_file2>, <out_file2>  and compare contents.
##one line description for the variables used
file_name = File which will be creating in the mount point intially.

data_file1 = File which contains data 'ABCDEFGHIJ'

image1 = First file snapshot which has 'ABCDEFGHIJ' + some null values.

data_file2 = File which contains data '1234567890'

image2 = second file snapshot which has '1234567890' + some null values.

out_file1 = After reverting image1 this contains 'ABCDEFGHIJ'

out_file2 = After reverting image2 this contians '1234567890'
