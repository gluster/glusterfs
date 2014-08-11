Introduction
============

This document goes through the new design of distributed geo-replication, it's features and the nature of changes involved. First we list down some of the important features.

  - Distributed asynchronous replication
  - Fast and versatile change detection
  - Replica failover
  - Hardlink synchronization
  - Effective handling of deletes and renames
  - Configurable sync engine (rsync, tar+ssh)
  - Adaptive to a wide variety of workloads
  - GFID synchronization

Geo-replication makes use of the all new *journaling* infrastructure (a.k.a. changelog) to achieve great performance and feature improvements as mentioned above. To understand more about changelogging and the helper library (*libgfchangelog*) refer to document: doc/features/geo-replication/libgfchangelog.md

Data Replication
----------------

Geo-replication is responsible to incrementally replicate data from the master node to the slave. But isn't that similar to what AFR does? Yes, but here the slave is located geographically distant from the master. Geo-replication follows the eventually consistent replication model, which implies, at any point of time, the slave would be lagging w.r.t. master, but would eventually catch up. Replication performance is dependent on two crucial factors:
  - Network latency
  - Change detection

Network latency is something that is not in direct control for many reasons, but still there is always a best effort. Therefore, geo-replication offloads the data replicaiton part to common UNIX file transfer utilities. We choose the grand daddy of file transfers [rsync(1)] [1] as the default synchronization engine, as it's best known for it's diff transfer algorithm for effcient usage of network and lightning fast transfers (leave alone the flexibiliy). But what about small files performance? Due to it's checksumming algorithm, rsync has more overhead for small files -- the overhead of checksumming outweighs the bytes to be transferred for small files. Therefore, geo-replication can also use combination of tar piped over ssh to transfer large number of small files. Tests have shown a great improvement over standard rsync. However, sync engine is not yet dynamic to the file type and needs to be chosen manually by a configuration option.

OTOH, change detection is something that is in full control of the application. Earlier (< release 3.5), geo-replicaiton would perform a file system crawl to indentify changes in the file system. This was not an unintelligent *check-every-single-inode* in the file system, but crawl logic based on *xtime*. xtime is an extended attribute maintained by the *marker* translator for each inode on the master and follows an upward-recursive marking pattern. Geo-replication would traverse a directory based on this simple condition:

> xtime(master) > xtime(slave)

E.g.:

>                    MASTER                          SLAVE
>
>                      /\                             /\
>                    d0  dir0                        d0 dir0
>                   /      \                        /     \
>                 d1       dir1                   d1      dir1
>                /                               /
>              d2                              d2
>             /                               /
>          file0                           file0

Consider the directory tree above. Assume that master and slave were in sync and the following operation happens on master:
```
touch /d0/d1/d2/file0
```
This would trigger a xtime marking (xtime being the current timestamp) from the leaf (*file0*) upto the root (*/*), i.e. an *xattr* of *file0*, *d2*, *d1*, *d0* and finally */*. Geo-replication daemon would crawl the file system based the condition mentioned before and hence would only crawl the **left** part of the directory tree (as the **right** part would hve equal xtimes).

Although the above crawling algorithm is fast, it still has to crawl a good part of the file system. Also, to decide whether to crawl a particular subdirectory, geo-rep need to compare xtime -- which is basically a **getxattr()** call on the master and slave (remember, *slave* is over a WAN).

Therefore, in 3.5 the need arised to take crawling to the next level. Geo-replication now uses the changelogging infrastructure to idenitify changes in the filesystem. Actually, there is absolutely no crawl involved. Changelogging based detection is notification based. Geo-replication daemon registers itself with the changelog consumer library (*libgfchangelog*) and basically invokes a set of APIs to get the list of changes in the filesystem and replays them onto the slave. There is absolutely no crawl or any kind of extended attribute gets involved.

Distributed Geo-Replication
---------------------------
Geo-replication (also known as gsyncd or geo-rep) used to be non-distributed before release 3.5. The node on which geo-rep start command was executed was responsible for replication data to the slave. If this node goes offline due to some reason (reboot, crash, etc..), replication would thereby be ceased. So one of the main development efforts for release 3.5 was to *distributify* geo-replication. Geo-rep daemon running on each node (per brick) is responsible for replicating data **local** to each brick. This results in full parallelism and effective use of cluster/network resource.

With release 3.5, geo-rep start command would spawn a geo-replication daemon on each node in the master cluster (one per brick). Geo-rep *status* command shown geo-rep session status from each master node. Similary, *stop* would gracefully tear down the session from all nodes.

What else is synced?
--------------------
  - GFID: Synchronizing the inode number (GFID) between master and the slave helps in synchronizing hardlinks.
  - Purges are also handled effectively as there is no entry comparison between master and slave. With changelog replay, geo-rep perform unlink operation without having to resort to expensive **readdir()** over the WAN.
  - Renames: With earlier geo-replication, because of the path based nature of crawling, renames were actually a delete and a create on the slave, followed by data transfer (not to mention the inode number change). Now, with changelogging, it's actually a **rename()** call on the slave.

Replica Failover
----------------
One of the basic volume configuration is a replicated volume (synchronous replication). Having geo-replication sync data from all replicas would mean wastage of network bandwidth and possibly data corruption on the slave (though that's unlikely). Therefore, geo-rep on such volume configurations works in an **ACTIVE** and **PASSIVE** mode. Geo-rep daemon on one of the replicas is responsible for replicating data (**ACTIVE**), while the other geo-rep daemon is basically doing nothing (**PASSIVE**).

On the event of the *ACTIVE* node going offline, the *PASSIVE* node identifies this event (there's a lag of max 60 seconds for this identification) and switches to *ACTIVE*; thereby taking over the role of replicating data from where the earlier *ACTIVE* node left off. This guarantees uninterrupted data replication even on node reboot/failures.

[1]:http://rsync.samba.org
