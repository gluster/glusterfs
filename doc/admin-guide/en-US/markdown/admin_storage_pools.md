#Setting up Trusted Storage Pools

Before you can configure a GlusterFS volume, you must create a trusted
storage pool consisting of the storage servers that provides bricks to a
volume.

A storage pool is a trusted network of storage servers. When you start
the first server, the storage pool consists of that server alone. To add
additional storage servers to the storage pool, you can use the probe
command from a storage server that is already trusted.

> **Note**: Do not self-probe the first server/localhost from itself.

The GlusterFS service must be running on all storage servers that you
want to add to the storage pool. See ? for more information.

##Adding Servers to Trusted Storage Pool

To create a trusted storage pool, add servers to the trusted storage
pool

1.  **The servers used to create the storage pool must be resolvable by
    hostname.**

    To add a server to the storage pool:

    `# gluster peer probe `

    For example, to create a trusted storage pool of four servers, add
    three servers to the storage pool from server1:

        # gluster peer probe server2
        Probe successful

        # gluster peer probe server3
        Probe successful

        # gluster peer probe server4
        Probe successful

2.  **Verify the peer status from the first server using the following
    commands:**

        # gluster peer status
        Number of Peers: 3

        Hostname: server2
        Uuid: 5e987bda-16dd-43c2-835b-08b7d55e94e5
        State: Peer in Cluster (Connected)

        Hostname: server3
        Uuid: 1e0ca3aa-9ef7-4f66-8f15-cbc348f29ff7
        State: Peer in Cluster (Connected)

        Hostname: server4
        Uuid: 3e0caba-9df7-4f66-8e5d-cbc348f29ff7
        State: Peer in Cluster (Connected)

    3.  **Assign the hostname to the first server by probing it from another server (not the server used in steps 1 and 2):**

        server2# gluster peer probe server1
        Probe successful

4.  **Verify the peer status from the same server you used in step 3 using the following
    command:**

        server2# gluster peer status
        Number of Peers: 3

        Hostname: server1
        Uuid: ceed91d5-e8d1-434d-9d47-63e914c93424
        State: Peer in Cluster (Connected)

        Hostname: server3
        Uuid: 1e0ca3aa-9ef7-4f66-8f15-cbc348f29ff7
        State: Peer in Cluster (Connected)

        Hostname: server4
        Uuid: 3e0caba-9df7-4f66-8e5d-cbc348f29ff7
        State: Peer in Cluster (Connected)

##Removing Servers from the Trusted Storage Pool

To remove a server from the storage pool:

`# gluster peer detach`

For example, to remove server4 from the trusted storage pool:

    # gluster peer detach server4
    Detach successful
