##### dht-fix-GFID-mismatch

The `fix_shard_linkto_gfid.sh` script handle the case of GFID mismatch between 'data' and 'linkto' files of a shard file. 
It detects all affected linkto shards and generate per subvolume:
1 - report with all shards that are having a GFID mismatch between 'data' and 'linkto' files. 
2 - bash script for correcting these affected file.
##### Usage:
###### Detection-Stage: run for getting the report and the fixing-script without running.
1. Copy `fix_shard_linkto_gfid.sh` to one of the cluster nodes (recomanded to copy to Brick1)
    ```
    scp fix_shard_linkto_gfid.sh Brick1_ip_or_hostname:/tmp/.
    ```
2. Run the script as root user, the -v <volume name> option is mandatory
    ```
    ./fix_shard_linkto_gfid.sh -v <volume name>
    ```
    Provides a report with all shards files with a GFID mismatch between 'data' and 'linkto' files. 
    The report per subvol is created under `/tmp/dht/` 
    
3. At the end of execution the script will create report file per subvol. 
   The report file contains all the linkto files with a a GFID mismatch. It includes the: filename, data/linkto gfids and theirs locations.
    ```
    # ll /tmp/dht/linkto_gfid*
    total 8
    -rw-r--r--. 1 root root  65 Mar 10 18:39 linkto_gfid_mismatch_subvol0
    -rw-r--r--. 1 root root 883 Mar 10 18:39 linkto_gfid_mismatch_subvol1
    # cat /tmp/dht/linkto_gfid_mismatch_subvol0
    filename    linkto-gfid              subvol       data-gfid                  subvol
    ---------------------------------------------------------------------------------------------
    ff.12,     0x57126af395f44c9dab8fc4, subvol_0,    0x57126af395f44c9dab8fc4f, subvol_1    
    xx.34,     0x15089ec2a3a5488293dd24, subvol_0,    0x15089ec2a3a5488293dd242, subvol_1     
    ```
4. At the end of execution the script will create a fixing script per subvol. 
   The script contains the commands for coreecting the affected files and can be run manually. 
     ``` 
    ll FIX_gfid_subvol_*
    -rwxr-xr-x. 1 root root 1679 Mar 11 19:34 FIX_gfid_subvol_0
    -rwxr-xr-x. 1 root root   13 Mar 11 19:34 FIX_gfid_subvol_1
    
    # cat FIX_gfid_subvol_0
    #! /bin/bash
    ssh 10.74.180.10 << FIX
        cd /mnt/data1/1/.shard
        setfattr -x "trusted.gfid" alex.sometime.crazy
        mv ../.glusterfs/64/57/6457a279-ff0e-4828-a021-e45f10c1bae3 /var/log/glusterfs/
        setfattr -n "trusted.gfid" -v 0xb71646556c1245b5a6b5c475976e8888 alex.sometime.crazy
    FIX
    
    ```

###### Fixing Stage: at the end of the detection stage it also runs the fixinf script(s)
1. using the option -f to fix affected linkto file GFID
```
    ./fix_shard_linkto_gfid.sh -v <volume name>
```

##### Following are the guidelines/suggestions to use these scripts.

1 - Passwordless ssh should be setup for all the nodes of the cluster.
2 - Scripts should be executed from one of these nodes. Recomandation to run on Brick1.
3 - Make sure NO "IO" is going on for the detected files.
4 - All the bricks of the volume should be UP.


###### Logs and other files
1 - logging to stdout (todo: to logfile)
2 - report and script files under /tmp/dht/ (todo: change to /var/log/glusterds/dht-script)
Report name: linkto_gfid_mismatch_subvol$i
Fix script: FIX_gfid_subvol_$i
