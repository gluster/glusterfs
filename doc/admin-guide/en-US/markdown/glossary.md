Glossary
========

**Brick**
:   A Brick is the basic unit of storage in GlusterFS, represented by an
    export directory on a server in the trusted storage pool. A Brick is
    represented by combining a server name with an export directory in the
    following format:

    `SERVER:EXPORT`

    For example:

    `myhostname:/exports/myexportdir/`

**Client**
:   Any machine that mounts a GlusterFS volume.

**Cluster**
:   A cluster is a group of linked computers, working together closely
    thus in many respects forming a single computer.

**Distributed File System**
:   A file system that allows multiple clients to concurrently access
    data over a computer network.

**Extended Attributes**
:   Extended file attributes (abbreviated xattr) is a file system feature
    that enables users/programs to associate files/dirs with metadata.

**Filesystem**
:   A method of storing and organizing computer files and their data.
    Essentially, it organizes these files into a database for the
    storage, organization, manipulation, and retrieval by the computer's
    operating system.

    Source: [Wikipedia][]

**FUSE**
:   Filesystem in Userspace (FUSE) is a loadable kernel module for
    Unix-like computer operating systems that lets non-privileged users
    create their own file systems without editing kernel code. This is
    achieved by running file system code in user space while the FUSE
    module provides only a "bridge" to the actual kernel interfaces.

    Source: [Wikipedia][1]

**Geo-Replication**
:   Geo-replication provides a continuous, asynchronous, and incremental
    replication service from site to another over Local Area Networks
    (LAN), Wide Area Network (WAN), and across the Internet.

**GFID**
:   Each file/directory on a GlusterFS volume has a unique 128-bit number
    associated with it called the GFID. This is analogous to inode in a
    regular filesystem.

**glusterd**
:   The Gluster management daemon that needs to run on all servers in
    the trusted storage pool.

**Infiniband**
    InfiniBand is a switched fabric computer network communications link
    used in high-performance computing and enterprise data centers.

**Metadata**
:   Metadata is data providing information about one or more other
    pieces of data.

**Namespace**
:   Namespace is an abstract container or environment created to hold a
    logical grouping of unique identifiers or symbols. Each Gluster
    volume exposes a single namespace as a POSIX mount point that
    contains every file in the cluster.

**Node**
:   A server or computer that hosts one or more bricks.

**Open Source**
:   Open source describes practices in production and development that
    promote access to the end product's source materials. Some consider
    open source a philosophy, others consider it a pragmatic
    methodology.

    Before the term open source became widely adopted, developers and
    producers used a variety of phrases to describe the concept; open
    source gained hold with the rise of the Internet, and the attendant
    need for massive retooling of the computing source code.

    Opening the source code enabled a self-enhancing diversity of
    production models, communication paths, and interactive communities.
    Subsequently, a new, three-word phrase "open source software" was
    born to describe the environment that the new copyright, licensing,
    domain, and consumer issues created.

    Source: [Wikipedia][2]

**Petabyte**
:   A petabyte (derived from the SI prefix peta- ) is a unit of
    information equal to one quadrillion (short scale) bytes, or 1000
    terabytes. The unit symbol for the petabyte is PB. The prefix peta-
    (P) indicates a power of 1000:

    1 PB = 1,000,000,000,000,000 B = 10005 B = 1015 B.

    The term "pebibyte" (PiB), using a binary prefix, is used for the
    corresponding power of 1024.

    Source: [Wikipedia][3]

**POSIX**
:   Portable Operating System Interface (for Unix) is the name of a
    family of related standards specified by the IEEE to define the
    application programming interface (API), along with shell and
    utilities interfaces for software compatible with variants of the
    Unix operating system. Gluster exports a fully POSIX compliant file
    system.

**Quorum**
:   The configuration of quorum in a trusted storage pool determines the
    number of server failures that the trusted storage pool can sustain.
    If an additional failure occurs, the trusted storage pool becomes
    unavailable.

**Quota**
:   Quotas allow you to set limits on usage of disk space by directories or
    by volumes.

**RAID**
:   Redundant Array of Inexpensive Disks (RAID) is a technology that
    provides increased storage reliability through redundancy, combining
    multiple low-cost, less-reliable disk drives components into a
    logical unit where all drives in the array are interdependent.

**RDMA**
:   Remote direct memory access (RDMA) is a direct memory access from the
    memory of one computer into that of another without involving either
    one's operating system. This permits high-throughput, low-latency
    networking, which is especially useful in massively parallel computer
    clusters.

**Rebalance**
:   A process of fixing layout and resdistributing data in a volume when a
    brick is added or removed.

**RRDNS**
:   Round Robin Domain Name Service (RRDNS) is a method to distribute
    load across application servers. RRDNS is implemented by creating
    multiple A records with the same name and different IP addresses in
    the zone file of a DNS server.

**Samba**
:   Samba allows file and print sharing between computers running Windows and
    computers running Linux. It is an implementation of several services and
    protocols including SMB and CIFS.

**Self-Heal**
:   The self-heal daemon that runs in the background, identifies
    inconsistencies in files/dirs in a replicated volume and then resolves
    or heals them. This healing process is usually required when one or more
    bricks of a volume goes down and then comes up later.

**Split-brain**
:   This is a situation where data on two or more bricks in a replicated
    volume start to diverge in terms of content or metadata. In this state,
    one cannot determine programitically which set of data is "right" and
    which is "wrong".

**Translator**
:   Translators (also called xlators) are stackable modules where each
    module has a very specific purpose. Translators are stacked in a
    hierarchical structure called as graph. A translator receives data
    from its parent translator, performs necessary operations and then
    passes the data down to its child translator in hierarchy.

**Trusted Storage Pool**
:   A storage pool is a trusted network of storage servers. When you
    start the first server, the storage pool consists of that server
    alone.

**Userspace**
:   Applications running in user space don’t directly interact with
    hardware, instead using the kernel to moderate access. Userspace
    applications are generally more portable than applications in kernel
    space. Gluster is a user space application.

**Volfile**
:   Volfile is a configuration file used by glusterfs process. Volfile
    will be usually located at `/var/lib/glusterd/vols/VOLNAME`.

**Volume**
:   A volume is a logical collection of bricks. Most of the gluster
    management operations happen on the volume.

  [Wikipedia]: http://en.wikipedia.org/wiki/Filesystem
  [1]: http://en.wikipedia.org/wiki/Filesystem_in_Userspace
  [2]: http://en.wikipedia.org/wiki/Open_source
  [3]: http://en.wikipedia.org/wiki/Petabyte
