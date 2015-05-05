Usage guide: Replicate volumes with arbiter configuration
==========================================================
Arbiter volumes are replica 3 volumes where the 3rd brick of the replica is
automatically configured as an arbiter node. What this means is that the 3rd
brick will store only the file name and metadata, but does not contain any data.
This configuration is helpful in avoiding split-brains while providing the same
level of consistency as a normal replica 3 volume.

The arbiter volume can be created with the following command:
`gluster volume create <VOLNAME>  replica 3 arbiter 1 host1:brick1 host2:brick2 host3:brick3`

Note that the syntax is similar to creating a normal replica 3 volume with the
exception of the `arbiter 1` keyword. As seen in the command above, the only
permissible values for the replica count and arbiter count are 3 and 1
respectively. Also, the 3rd brick is always chosen as the arbiter brick and it
is not configurable to have any other brick as the arbiter.

Client/ Mount behaviour:
========================
By default, client quorum (`cluster.quorum-type`) is set to `auto` for a replica
3 volume when it is created;  i.e. at least 2 bricks need to be up to satisfy
quorum and to allow writes. This setting is not to be changed for arbiter
volumes also. Additionally, the arbiter volume has additional some checks to
prevent files from ending up in split-brain:

* Clients take full file locks when writing to a file as opposed to range locks
  in a normal replica 3 volume.

* If 2 bricks are up and if one of them is the arbiter (i.e. the 3rd brick) *and*
  it blames the other up brick, then all FOPS will fail with ENOTCONN (Transport
  endpoint is not connected). IF the arbiter doesn't blame the other brick,
  FOPS will be allowed to proceed. 'Blaming' here is w.r.t the values of AFR
  changelog extended attributes.

* If 2 bricks are up and the arbiter is down, then FOPS will be allowed.

* In all cases, if there is only one source before the FOP is initiated and if
  the FOP fails on that source, the application will receive ENOTCONN.

Note: It is possible to see if a replica 3 volume has arbiter configuration from
the mount point. If
`$mount_point/.meta/graphs/active/$V0-replicate-0/options/arbiter-count` exists
and its value is 1, then it is an arbiter volume. Also the client volume graph
will have arbiter-count as a xlator option for AFR translators.

Self-heal daemon behaviour:
===========================
Since the arbiter brick does not store any data for the files, data-self-heal
from the arbiter brick will not take place. For example if there are 2 source
bricks B2 and B3 (B3 being arbiter brick) and B2 is down, then data-self-heal
will *not* happen from B3 to sink brick B1, and will be pending until B2 comes
up and heal can happen from it. Note that  metadata and entry self-heals can
still happen from B3 if it is one of the sources.
