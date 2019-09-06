# gluster-heal-scripts
Scripts to correct extended attributes of fragments of files to make them healble.

Following are the guidelines/suggestions to use these scripts.

1 - Passwordless ssh should be setup for all the nodes of the cluster.

2 - Scripts should be executed from one of these nodes.

3 - Make sure NO "IO" is going on for the files for which we are running
these two scripts.

4 - There should be no heal going on for the file for which xattrs are being
set by correct_pending_heals.sh. Disable the self heal while running this script.

5 - All the bricks of the volume should be UP to identify good and bad fragments
and to decide if an entry is healble or not.

6 - If correct_pending_heals.sh is stopped in the middle while it was processing
healble entries, it is suggested to re-run gfid_needing_heal_parallel.sh to create
latest list of healble and non healble entries and "potential_heal" "can_not_heal" files.

7 - Based on the number of entries, these files might take time to get and set the
stats and xattrs of entries.

8 - A backup of the fragments will be taken on <brick path>/.glusterfs/correct_pending_heals
    directory with a file name same as gfid.

9 - Once the correctness of the file gets verified by user, these backup should be removed.

10 - Make sure we have enough space on bricks to take these backups.

11 - At the end this will create two files -
     1 - modified_and_backedup_files - Contains list of files which have been modified and should be healed.
     2 - can_not_heal - Contains list of files which can not be healed.

12 - It is suggested that the integrity of the data of files, which were modified and healed,
     should be checked by the user.


Usage:

Following are the sequence of steps to use these scripts -

1 - ./gfid_needing_heal_parallel.sh <volume name>

    Execute gfid_needing_heal_parallel.sh with volume name to create list of files which could
    be healed and can not be healed. It creates "potential_heal" and "can_not_heal" files.
    During execution, it also displays the list of files on consol with the verdict.

2 - ./correct_pending_heals.sh

    Execute correct_pending_heals.sh without any argument. This script processes entries present
    in "heal" file. It asks user to enter how many files we want to process in one attempt.
    Once the count is provided, this script will fetch the entries one by one from "potential_heal" file and takes necessary action.
    If at this point also a file can not be healed, it will be pushed to "can_not_heal" file.
    If a file can be healed, this script will modify the xattrs of that file fragments and create an entry in "modified_and_backedup_files" file

3 - At the end, all the entries of "potential_heal" will be processed and based on the processing only two files will be left.

     1 - modified_and_backedup_files - Contains list of files which have been modified and should be healed.
     2 - can_not_heal - Contains list of files which can not be healed.

Logs and other files -

1 - modified_and_backedup_files  - It contains all the files which could be healed and the location of backup of each fragments.
2 - can_not_heal - It contains all the files which can not be healed.
3 - potential_heal - List of files which could be healed and should be processed by "correct_pending_heals.sh"
4 - /var/log/glusterfs/ec-heal-script.log - It contains logs of both the files.
