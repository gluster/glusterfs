#Setting up GlusterFS Server Volumes

A volume is a logical collection of bricks where each brick is an export
directory on a server in the trusted storage pool. Most of the gluster
management operations are performed on the volume.

To create a new volume in your storage environment, specify the bricks
that comprise the volume. After you have created a new volume, you must
start it before attempting to mount it.

-   Volumes of the following types can be created in your storage
    environment:

    -   **Distributed** - Distributed volumes distributes files throughout
        the bricks in the volume. You can use distributed volumes where
        the requirement is to scale storage and the redundancy is either
        not important or is provided by other hardware/software layers.

    -   **Replicated** – Replicated volumes replicates files across bricks
        in the volume. You can use replicated volumes in environments
        where high-availability and high-reliability are critical.

    -   **Striped** – Striped volumes stripes data across bricks in the
        volume. For best results, you should use striped volumes only in
        high concurrency environments accessing very large files.

    -   **Distributed Striped** - Distributed striped volumes stripe data
        across two or more nodes in the cluster. You should use
        distributed striped volumes where the requirement is to scale
        storage and in high concurrency environments accessing very
        large files is critical.

    -   **Distributed Replicated** - Distributed replicated volumes
        distributes files across replicated bricks in the volume. You
        can use distributed replicated volumes in environments where the
        requirement is to scale storage and high-reliability is
        critical. Distributed replicated volumes also offer improved
        read performance in most environments.

    -   **Distributed Striped Replicated** – Distributed striped replicated
        volumes distributes striped data across replicated bricks in the
        cluster. For best results, you should use distributed striped
        replicated volumes in highly concurrent environments where
        parallel access of very large files and performance is critical.
        In this release, configuration of this volume type is supported
        only for Map Reduce workloads.

    -   **Striped Replicated** – Striped replicated volumes stripes data
        across replicated bricks in the cluster. For best results, you
        should use striped replicated volumes in highly concurrent
        environments where there is parallel access of very large files
        and performance is critical. In this release, configuration of
        this volume type is supported only for Map Reduce workloads.

**To create a new volume**

-   Create a new volume :

    `# gluster volume create [stripe  | replica ] [transport tcp | rdma | tcp, rdma] `

    For example, to create a volume called test-volume consisting of
    server3:/exp3 and server4:/exp4:

        # gluster volume create test-volume server3:/exp3 server4:/exp4
        Creation of test-volume has been successful
        Please start the volume to access data.

##Creating Distributed Volumes

In a distributed volumes files are spread randomly across the bricks in
the volume. Use distributed volumes where you need to scale storage and
redundancy is either not important or is provided by other
hardware/software layers.

> **Note**:
> Disk/server failure in distributed volumes can result in a serious
> loss of data because directory contents are spread randomly across the
> bricks in the volume.

![][]

**To create a distributed volume**

1.  Create a trusted storage pool.

2.  Create the distributed volume:

    `# gluster volume create  [transport tcp | rdma | tcp,rdma] `

    For example, to create a distributed volume with four storage
    servers using tcp:

        # gluster volume create test-volume server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4
        Creation of test-volume has been successful
        Please start the volume to access data.

    (Optional) You can display the volume information:

        # gluster volume info
        Volume Name: test-volume
        Type: Distribute
        Status: Created
        Number of Bricks: 4
        Transport-type: tcp
        Bricks:
        Brick1: server1:/exp1
        Brick2: server2:/exp2
        Brick3: server3:/exp3
        Brick4: server4:/exp4

    For example, to create a distributed volume with four storage
    servers over InfiniBand:

        # gluster volume create test-volume transport rdma server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Creating Replicated Volumes

Replicated volumes create copies of files across multiple bricks in the
volume. You can use replicated volumes in environments where
high-availability and high-reliability are critical.

> **Note**:
> The number of bricks should be equal to of the replica count for a
> replicated volume. To protect against server and disk failures, it is
> recommended that the bricks of the volume are from different servers.

![][1]

**To create a replicated volume**

1.  Create a trusted storage pool.

2.  Create the replicated volume:

    `# gluster volume create  [replica ] [transport tcp | rdma tcp,rdma] `

    For example, to create a replicated volume with two storage servers:

        # gluster volume create test-volume replica 2 transport tcp server1:/exp1 server2:/exp2
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Creating Striped Volumes

Striped volumes stripes data across bricks in the volume. For best
results, you should use striped volumes only in high concurrency
environments accessing very large files.

> **Note**:
> The number of bricks should be a equal to the stripe count for a
> striped volume.

![][2]

**To create a striped volume**

1.  Create a trusted storage pool.

2.  Create the striped volume:

    `# gluster volume create  [stripe ] [transport tcp | rdma | tcp,rdma] `

    For example, to create a striped volume across two storage servers:

        # gluster volume create test-volume stripe 2 transport tcp server1:/exp1 server2:/exp2
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Creating Distributed Striped Volumes

Distributed striped volumes stripes files across two or more nodes in
the cluster. For best results, you should use distributed striped
volumes where the requirement is to scale storage and in high
concurrency environments accessing very large files is critical.

> **Note**:
> The number of bricks should be a multiple of the stripe count for a
> distributed striped volume.

![][3]

**To create a distributed striped volume**

1.  Create a trusted storage pool.

2.  Create the distributed striped volume:

    `# gluster volume create  [stripe ] [transport tcp | rdma | tcp,rdma] `

    For example, to create a distributed striped volume across eight
    storage servers:

        # gluster volume create test-volume stripe 4 transport tcp server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4 server5:/exp5 server6:/exp6 server7:/exp7 server8:/exp8
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Creating Distributed Replicated Volumes

Distributes files across replicated bricks in the volume. You can use
distributed replicated volumes in environments where the requirement is
to scale storage and high-reliability is critical. Distributed
replicated volumes also offer improved read performance in most
environments.

> **Note**:
> The number of bricks should be a multiple of the replica count for a
> distributed replicated volume. Also, the order in which bricks are
> specified has a great effect on data protection. Each replica\_count
> consecutive bricks in the list you give will form a replica set, with
> all replica sets combined into a volume-wide distribute set. To make
> sure that replica-set members are not placed on the same node, list
> the first brick on every server, then the second brick on every server
> in the same order, and so on.

![][4]

**To create a distributed replicated volume**

1.  Create a trusted storage pool.

2.  Create the distributed replicated volume:

    `# gluster volume create  [replica ] [transport tcp | rdma | tcp,rdma] `

    For example, four node distributed (replicated) volume with a
    two-way mirror:

        # gluster volume create test-volume replica 2 transport tcp server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4
        Creation of test-volume has been successful
        Please start the volume to access data.

    For example, to create a six node distributed (replicated) volume
    with a two-way mirror:

        # gluster volume create test-volume replica 2 transport tcp server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4 server5:/exp5 server6:/exp6
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Creating Distributed Striped Replicated Volumes

Distributed striped replicated volumes distributes striped data across
replicated bricks in the cluster. For best results, you should use
distributed striped replicated volumes in highly concurrent environments
where parallel access of very large files and performance is critical.
In this release, configuration of this volume type is supported only for
Map Reduce workloads.

> **Note**:
> The number of bricks should be a multiples of number of stripe count
> and replica count for a distributed striped replicated volume.

**To create a distributed striped replicated volume**

1.  Create a trusted storage pool.

2.  Create a distributed striped replicated volume using the following
    command:

    `# gluster volume create  [stripe ] [replica ] [transport tcp | rdma | tcp,rdma] `

    For example, to create a distributed replicated striped volume
    across eight storage servers:

        # gluster volume create test-volume stripe 2 replica 2 transport tcp server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4 server5:/exp5 server6:/exp6 server7:/exp7 server8:/exp8
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Creating Striped Replicated Volumes

Striped replicated volumes stripes data across replicated bricks in the
cluster. For best results, you should use striped replicated volumes in
highly concurrent environments where there is parallel access of very
large files and performance is critical. In this release, configuration
of this volume type is supported only for Map Reduce workloads.

> **Note**:
> The number of bricks should be a multiple of the replicate count and
> stripe count for a striped replicated volume.

![][5]

**To create a striped replicated volume**

1.  Create a trusted storage pool consisting of the storage servers that
    will comprise the volume.

2.  Create a striped replicated volume :

    `# gluster volume create  [stripe ] [replica ] [transport tcp | rdma | tcp,rdma] `

    For example, to create a striped replicated volume across four
    storage servers:

        # gluster volume create test-volume stripe 2 replica 2 transport tcp server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4
        Creation of test-volume has been successful
        Please start the volume to access data.

    To create a striped replicated volume across six storage servers:

        # gluster volume create test-volume stripe 3 replica 2 transport tcp server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4 server5:/exp5 server6:/exp6
        Creation of test-volume has been successful
        Please start the volume to access data.

    If the transport type is not specified, *tcp* is used as the
    default. You can also set additional options if required, such as
    auth.allow or auth.reject.

    > **Note**:
    > Make sure you start your volumes before you try to mount them or
    > else client operations after the mount will hang.

##Starting Volumes

You must start your volumes before you try to mount them.

**To start a volume**

-   Start a volume:

    `# gluster volume start `

    For example, to start test-volume:

        # gluster volume start test-volume
        Starting test-volume has been successful

  []: ../images/Distributed_Volume.png
  [1]: ../images/Replicated_Volume.png
  [2]: ../images/Striped_Volume.png
  [3]: ../images/Distributed_Striped_Volume.png
  [4]: ../images/Distributed_Replicated_Volume.png
  [5]: ../images/Striped_Replicated_Volume.png
