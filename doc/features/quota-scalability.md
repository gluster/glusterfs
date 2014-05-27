Issues with older implemetation:
-----------------------------------
* >#### Enforcement of quota was done on client side. This had following two issues :
  > >* All clients are not trusted and hence enforcement is not secure.
  > >* Quota enforcer caches directory size for a certain time out period to reduce network calls to fetch size. On time out, this cache is validated by querying server. With more clients, the traffic caused due to this
validation increases.

* >#### Relying on lookup calls on a file/directory (inode) to update its contribution [time consuming]

* >####Hardlimits were stored in a comma separated list.
  > >* Hence, changing hard limit of one directory is not an independent operation and would invalidate hard limits of other directories. We need to parse the string once for each of these directories just to identify whether its hard limit is changed. This limits the number of hard limits we can configure.

* >####Cli used to fetch the list of directories on which quota-limit is set, from glusterd.
  > >* With more number of limits, the network overhead incurred to fetch this list limits the scalability of number of directories on which we can set quota.

* >#### Problem with NFS mount
  > >*  Quota, for its enforcement and accounting requires all the ancestors of a file/directory till root. However, with NFS relying heavily on nameless lookups (through which there is no guarantee that ancestry can be
accessed) this ancestry is not always present. Hence accounting and enforcement was not correct.


New Design Implementation:
--------------------------------

* Quota enforcement is moved to server side. This addresses issues that arose because of client side enforcement.

* Two levels of quota limits, soft and hard quota is introduced.
  This will result in a message being logged on reaching soft quota and    writes will fail with EDQUOT after hard limit is reached.

Work Flow
-----------------

* Accounting
      # This is done using the marker translator loaded on each brick of the volume. Accounting happens in the background. Ie, it doesn't happen in-flight with the file operation. The file operations latency is not
directly affected by the time taken to perform accounting. This update is sent recursively upwards up to the root of the volume.

* Enforcement
      # The enforcer updates its 'view' (cached) of directory's disk usage on the incidence of a file operation after the expiry of hard/soft timeout, depending on the current usage. Enforcer uses quotad to get the
aggregated disk usage of a directory from the accounting information present on each brick (viz, provided by marker).

* Aggregator (quotad)
      # Quotad is a daemon that serves volume-wide disk usage of a directory, on which quota is configured. It is present on all nodes in the cluster (trusted storage pool) as bricks don't have a global view of cluster.
Quotad queries the disk usage information from all the bricks in that volume and aggregates. It manages all the volumes on which quota is enabled.


Benefit to GlusterFS
---------------------------------

* Support upto 65536 quota configurations per volume.
* More quotas can be configured in a single volume thereby leading to support GlusterFS for use cases like home directory.

###For more information on quota usability refer the following link :
> https://access.redhat.com/site/documentation/en-US/Red_Hat_Storage/2.1/html-single/Administration_Guide/index.html#chap-User_Guide-Dir_Quota-Enable
