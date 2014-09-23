## Rdma Connection manager ##

### What? ###
Infiniband requires addresses of end points to be exchanged using an out-of-band channel (like tcp/ip). Glusterfs used a custom protocol over a tcp/ip channel to exchange this address. However, librdmacm provides the same functionality with the advantage of being a standard protocol. This helps if we want to communicate with a non-glusterfs entity (say nfs client with gluster nfs server) over infiniband.

### Dependencies ###
* [IP over Infiniband](http://pkg-ofed.alioth.debian.org/howto/infiniband-howto-5.html) - The value to *option* **remote-host** in glusterfs transport configuration  should be an IPoIB address
* [rdma cm kernel module](http://pkg-ofed.alioth.debian.org/howto/infiniband-howto-4.html#ss4.4)
* [user space rdmacm library - librdmacm](https://www.openfabrics.org/downloads/rdmacm)

### rdma-cm in >= GlusterFs 3.4 ###

Following is the impact of http://review.gluster.org/#change,149.

New userspace packages needed:
librdmacm
librdmacm-devel

### Limitations ###

* Because of bug [890502](https://bugzilla.redhat.com/show_bug.cgi?id=890502), we've to probe the peer on an IPoIB address. This imposes a restriction that all volumes created in the future have to communicate over IPoIB address (irrespective of whether they use gluster's tcp or rdma transport).

* Currently client has independence to choose b/w tcp and rdma transports while communicating with the server (by creating volumes with **transport-type tcp,rdma**). This independence was a by-product of our ability to use the tcp/ip channel - transports with *option transport-type tcp* - for rdma connection establishment handshake too. However, with new requirement of IPoIB address for connection establishment, we loose this independence (till we bring in [multi-network support](https://bugzilla.redhat.com/show_bug.cgi?id=765437) - where a brick can be identified by a set of ip-addresses and we can choose different pairs of ip-addresses for communication based on our requirements - in glusterd).

### External links ###
* [Infiniband Howto](http://pkg-ofed.alioth.debian.org/howto/infiniband-howto.html)
