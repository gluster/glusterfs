# Server Quorum

Server quorum is a feature intended to reduce the occurrence of "split brain"
after a brick failure or network partition.  Split brain happens when different
sets of servers are allowed to process different sets of writes, leaving data
in a state that can not be reconciled automatically.  The key to avoiding split
brain is to ensure that there can be only one set of servers - a quorum - that
can continue handling writes.  Server quorum does this by the brutal but
effective means of forcing down all brick daemons on cluster nodes that can no
longer reach enough of their peers to form a majority.  Because there can only
be one majority, there can be only one set of bricks remaining, and thus split
brain can not occur.

## Options

Server quorum is controlled by two parameters:

 * **cluster.server-quorum-type**
 
   This value may be "server" to indicate that server quorum is enabled, or
   "none" to mean it's disabled.
	
 * **cluster.server-quorum-ratio**

   This is the percentage of cluster nodes that must be up to maintain quorum.
   More precisely, this percentage of nodes *plus one* must be up.

Note that these are cluster-wide flags.  All volumes served by the cluster will
be affected.  Once these values are set, quorum actions - starting or stopping
brick daemons in response to node or network events - will be automatic.

## Best Practices

If a cluster with an even number of nodes is split exactly down the middle,
neither half can have quorum (which requires **more than** half of the total).
This is particularly important when N=2, in which case the loss of either node
leads to loss of quorum.  Therefore, it is highly advisable to ensure that the
cluster size is three or greater.  The "extra" node in this case need not have
any bricks or serve any data.  It need only be present to preserve the notion
of a quorum majority less than the entire cluster membership, allowing the
cluster to survive the loss of a single node without losing quorum.



