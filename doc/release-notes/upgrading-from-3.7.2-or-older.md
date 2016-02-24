A new feature in 3.7.3 is causing troubles during upgrades from previous versions of GlusterFS to 3.7.3.
The details of the feature, issue and work around are below.

## Feature
In GlusterFS-3.7.3, insecure-ports have been enabled by default. This
means that by default, servers accept connections from insecure ports,
clients use insecure ports to connect to servers. This change
particularly benefits usage of libgfapi, for example when it is used
in qemu run by a normal user.

## Issue
This has caused troubles when upgrading from previous versions to
3.7.3 in rolling upgrades and when attempting to use 3.7.3 clients
with older servers. The 3.7.3 clients establish connections using
insecure ports by default. But the older servers still expect
connections to come from secure-ports (if this setting has not been
changed). This causes servers to reject connections from 3.7.3, and
leads to broken clusters during upgrade and rejected clients.

## Workaround
There are two possible workarounds.
Before upgrading,

1. Set 'client.bind-insecure off' on all volumes.
This forces 3.7.3 clients to use secure ports to connect to the servers.
This does not affect older clients as this setting is the default for them.

2. Set 'server.allow-insecure on' on all volumes.
This enables servers to accept connections from insecure ports.
The new clients can successfully connect to the servers with this set.


If anyone faces any problems with these workarounds, please let us know via email[1][1] or in IRC[2][2].


[1]: gluster-devel at gluster dot org / gluster-users at gluster dot org
[2]: #gluster / #gluster-dev @ freenode
