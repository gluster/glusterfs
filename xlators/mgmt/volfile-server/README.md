
# Volfile Server

This translator makes glusterfs start a process to only serve volfiles. It just appends `.vol` to whatever the volume name sent by the client processes and serves them if it exists.


### Usecase

There are efforts like https://github.com/kadalu/moana, which is starting to visualize glusterfs management layer differently. But considering, on the client side, kadalu doesn't make any changes, it has to provide option to provide volfile properly so `mount -t glusterfs` can be used as is compared to now.

With this service, which can be started on any node which doesn't have 24007 port binded (ie, no glusterd), one can serve gluster's volfiles smoothly, and also manage the automatic notification of changes to volfile (if any).

