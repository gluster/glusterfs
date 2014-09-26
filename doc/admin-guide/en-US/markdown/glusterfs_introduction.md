Introducing Gluster File System
===============================

GlusterFS is an open source, distributed file system capable of scaling to
several petabytes and handling thousands of clients. It is a file system with
a modular, stackable design, and a unique no-metadata server architecture.
This no-metadata server architecture ensures better performance,
linear scalability, and reliability. GlusterFS can be
flexibly combined with commodity physical, virtual, and cloud resources
to deliver highly available and performant enterprise storage at a
fraction of the cost of traditional solutions.

GlusterFS clusters together storage building blocks over Infiniband RDMA
and/or TCP/IP interconnect, aggregating disk and memory resources and
managing data in a single global namespace.

GlusterFS aggregates various storage servers over network interconnects
into one large parallel network file system. Based on a stackable user space
design, it delivers exceptional performance for diverse workloads and is a key
building block of GlusterFS.
The POSIX compatible GlusterFS servers, use any ondisk file system which supports
extended attributes (eg: ext4, XFS, etc) to format to store data on disks, can be
accessed using industry-standard access protocols including Network File System (NFS)
and Server Message Block (SMB).

![ Virtualized Cloud Environments ](../images/640px-GlusterFS_Architecture.png)

GlusterFS is designed for today's high-performance, virtualized cloud
environments. Unlike traditional data centers, cloud environments
require multi-tenancy along with the ability to grow or shrink resources
on demand. Enterprises can scale capacity, performance, and availability
on demand, with no vendor lock-in, across on-premise, public cloud, and
hybrid environments.

GlusterFS is in production at thousands of enterprises spanning media,
healthcare, government, education, web 2.0, and financial services.

## Commercial offerings and support ##

Several companies offer support or consulting - http://www.gluster.org/consultants/.

Red Hat Storage (http://www.redhat.com/en/technologies/storage/storage-server)
is a commercial storage software product, based on GlusterFS.


## About On-premise Installation ##

GlusterFS for On-Premise allows physical storage to be utilized as a
virtualized, scalable, and centrally managed pool of storage.

GlusterFS can be installed on commodity servers resulting in a
powerful, massively scalable, and highly available NAS environment.

GlusterFS On-premise enables enterprises to treat physical storage as a
virtualized, scalable, and centrally managed storage pool by using commodity
storage hardware. It supports multi-tenancy by partitioning users or groups into
logical volumes on shared storage. It enables users to eliminate, decrease, or
manage their dependence on high-cost, monolithic and difficult-to-deploy storage arrays.
You can add capacity in a matter of minutes across a wide variety of workloads without
affecting performance. Storage can also be centrally managed across a variety of
workloads, thus increasing storage efficiency.


