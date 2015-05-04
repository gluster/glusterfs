## Upcall ##

### Summary ###
A generic and extensible framework, used to maintain states in the glusterfsd process for each of the files accessed (including the clients info doing the fops) and send notifications to the respective glusterfs clients incase of any change in that state.

Few of the use-cases (currently using) this infrastructure are:

    Inode Update/Invalidation

### Detailed Description ###
GlusterFS, a scale-out storage platform, comprises of distributed file system which follows client-server architectural model.

Its the client(glusterfs) which usually initiates an rpc request to the server(glusterfsd). After processing the request, reply is sent to the client as response to the same request. So till now, there was no interface and use-case present for the server to intimate or make a request to the client.

This support is now being added using “Upcall Infrastructure”.

A new xlator(Upcall) has been defined to maintain and process state of the events which require server to send upcall notifications. For each I/O on a inode, we create/update a ‘upcall_inode_ctx’ and store/update the list of clients’ info ‘upcall_client_t’ in the context. 

#### Cache Invalidation ####
Each of the GlusterFS clients/applications cache certain state of the files (for eg, inode or attributes). In a muti-node environment these caches could lead to data-integrity issues, for certain time, if there are multiple clients accessing the same file simulataneously.
To avoid such scenarios, we need server to notify clients incase of any change in the file state/attributes. 

More details can be found in the below links - 
        http://www.gluster.org/community/documentation/index.php/Features/Upcall-infrastructure
        https://soumyakoduri.wordpress.com/2015/02/25/glusterfs-understanding-upcall-infrastructure-and-cache-invalidation-support/

cache-invalidation is currently disabled by default. It can be enabled with the following command:

    gluster volume set <volname> features.cache-invalidation on

Note: This upcall notification is sent to only those clients which have accessed the file recently (i.e, with in CACHE_INVALIDATE_PERIOD – default 60sec). This options can be tuned using the following command:

    gluster volume set <volname> features.cache-invalidation-timeout <value>
