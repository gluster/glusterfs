Self-Heal Daemon
================
The self-heal daemon (shd) is a glusterfs process that is responsible for healing files in a replicate/ disperse gluster volume.
Every server (brick) node of the volume runs one instance of the shd. So even if one node contains replicate/ disperse bricks of
multiple volumes, it would be healed by the same shd.

This document only describes how the shd works for replicate (AFR) volumes.

The shd is launched by glusterd when the volume starts (only if the volume includes a replicate configuration). The graph
of the shd process in every node contains the following: The io-stats which is the top most xlator, its children being the
replicate xlators (subvolumes) of *only* the bricks present in that particular node, and finally *all* the client xlators that are the children of the replicate xlators.

The shd does two types of self-heal crawls: Index heal and Full heal. For both these types of crawls, the basic idea is the same:  
For each file encountered while crawling, perform metadata, data and entry heals under appropriate locks.  
* An overview of how each of these heals is performed is detailed in the 'Self-healing' section of *doc/features/afr-v1.md*
* The different file locks which the shd takes for each of these heals is detailed in *doc/developer
-guide/afr-locks-evolution.md*

Metadata heal refers to healing extended attributes, mode and permissions of a file or directory.
Data heal refers to healing the file contents.
Entry self-heal refers to healing entries inside a directory.

Index heal
==========
The index heal is done:  
  a) Every 600 seconds (can be changed via the `cluster.heal-timeout` volume option)  
  b) When it is explicitly triggered via the `gluster vol heal <VOLNAME>` command  
  c) Whenever a replica brick that was down comes back up.  
  
Only one heal can be in progress at one time, irrespective of reason why it was triggered. If another heal is triggered before the first one completes, it will be queued.
Only one heal can be queued while the first one is running. If an Index heal is queued, it can be overridden by queuing a Full heal and not vice-versa.  Also, before processing
each entry in index heal, a check is made if a full heal is queued. If it is, then the index heal is aborted so that the full heal can proceed. 

In index heal, each shd reads the entries present inside .glusterfs/indices/xattrop/ folder and triggers heal on each entry with appropriate locks.
The .glusterfs/indices/xattrop/ directory contains a base entry of the name "xattrop-<virtual-gfid-string>". All other entries are hardlinks to the base entry. The
*names* of the hardlinks are the gfid strings of the files that may need heal. 

When a client (mount) performs an operation on the file, the index xlator present in each brick process adds the hardlinks in the pre-op phase of the FOP's transaction
and removes it in post-op phase if the operation is successful. Thus if an entry is present inside the .glusterfs/indices/xattrop/ directory when there is no I/O 
happening on the file, it means the file needs healing (or atleast an examination if the brick crashed after the post-op completed but just before the removal of the hardlink).

####Index heal steps:
<pre><code>
In shd process of *each node* {
        opendir +readdir (.glusterfs/indices/xattrop/)
        for each entry inside it {
                self_heal_entry() //Explained below.
        }
}
</code></pre>

<pre><code>
self_heal_entry() {
        Call syncop_lookup(replicae subvolume) which eventually does {
                take appropriate locks
                determine source and sinks from AFR changelog xattrs	
                perform whatever heal is needed (any of metadata, data and entry heal in that order)
                clear changelog xattrs and hardlink inside .glusterfs/indices/xattrop/
        }
}
</code></pre>

Note:
* If the gfid hardlink is present in the .glusterfs/indices/xattrop/ of both replica bricks, then each shd will try to heal the file but only one of them will be able to proceed due to the self-heal domain lock.

* While processing entries inside .glusterfs/indices/xattrop/, if shd encounters an entry whose parent is yet to be healed, it will skip it and it will be picked up in the next crawl.

* If a file is in data/ metadata split-brain, it will not be healed.

* If a directory is in entry split-brain, a conservative merge will be performed, wherein after the merge, the entries of the directory will be a union of the entries in the replica pairs.

Full heal
=========
A full heal is triggered by running `gluster vol heal <VOLNAME> full`. This command is usually run in disk replacement scenarios where the entire data is to be copied from one of the healthy bricks of the replica to the brick that was just replaced.

Unlike the index heal which runs on the shd of every node in a replicate subvolume, the full heal is run only on the shd of one node per replicate subvolume: the node having the highest UUID.
i.e In a 2x2 volume made of 4 nodes N1, N2, N3 and N4, If UUID of N1>N2 and UUID N4 >N3, then the full crawl is carried out by the shds of N1 and N4.(Node UUID can be found in `/var/lib/glusterd/glusterd.info`)

The full heal steps are almost identical to the index heal, except the heal is performed on each replica starting from the root of the volume:
<pre><code>
In shd process of *highest UUID node per replica* {
        opendir +readdir ("/")
        for each entry inside it {
                self_heal_entry()
                if (entry == directory) {
                        /* Recurse*/
                        again opendir+readdir (directory) followed by self_heal_entry() of each entry.
                }
                
        }
}
</code></pre>
