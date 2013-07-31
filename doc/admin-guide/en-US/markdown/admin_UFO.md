Managing Unified File and Object Storage
========================================

Unified File and Object Storage (UFO) unifies NAS and object storage
technology. It provides a system for data storage that enables users to
access the same data, both as an object and as a file, thus simplifying
management and controlling storage costs.

Unified File and Object Storage is built upon Openstack's Object Storage
Swift. Open Stack Object Storage allows users to store and retrieve
files and content through a simple Web Service (REST: Representational
State Transfer) interface as objects and GlusterFS, allows users to
store and retrieve files using Native Fuse and NFS mounts. It uses
GlusterFS as a backend file system for Open Stack Swift. It also
leverages on Open Stack Swift's web interface for storing and retrieving
files over the web combined with GlusterFS features like scalability and
high availability, replication, elastic volume management for data
management at disk level.

Unified File and Object Storage technology enables enterprises to adopt
and deploy cloud storage solutions. It allows users to access and modify
data as objects from a REST interface along with the ability to access
and modify files from NAS interfaces including NFS and CIFS. In addition
to decreasing cost and making it faster and easier to access object
data, it also delivers massive scalability, high availability and
replication of object storage. Infrastructure as a Service (IaaS)
providers can utilize GlusterFS Unified File and Object Storage
technology to enable their own cloud storage service. Enterprises can
use this technology to accelerate the process of preparing file-based
applications for the cloud and simplify new application development for
cloud computing environments.

OpenStack Object Storage is scalable object storage system and it is not
a traditional file system. You will not be able to mount this system
like traditional SAN or NAS volumes and perform POSIX compliant
operations.

Components of Object Storage
============================

The major components of Object Storage are:

**Proxy Server**

All REST requests to the UFO are routed through the Proxy Server.

**Objects and Containers**

An object is the basic storage entity and any optional metadata that
represents the data you store. When you upload data, the data is stored
as-is (with no compression or encryption).

A container is a storage compartment for your data and provides a way
for you to organize your data. Containers can be visualized as
directories in a Linux system. Data must be stored in a container and
hence objects are created within a container.

It implements objects as files and directories under the container. The
object name is a '/' separated path and UFO maps it to directories until
the last name in the path, which is marked as a file. With this
approach, objects can be accessed as files and directories from native
GlusterFS (FUSE) or NFS mounts by providing the '/' separated path.

**Accounts and Account Servers**

The OpenStack Object Storage system is designed to be used by many
different storage consumers. Each user is associated with one or more
accounts and must identify themselves using an authentication system.
While authenticating, users must provide the name of the account for
which the authentication is requested.

UFO implements accounts as GlusterFS volumes. So, when a user is granted
read/write permission on an account, it means that that user has access
to all the data available on that GlusterFS volume.

**Authentication and Access Permissions**

You must authenticate against an authentication service to receive
OpenStack Object Storage connection parameters and an authentication
token. The token must be passed in for all subsequent container or
object operations. One authentication service that you can use as a
middleware example is called `tempauth`.

By default, each user has their own storage account and has full access
to that account. Users must authenticate with their credentials as
described above, but once authenticated they can manage containers and
objects within that account. If a user wants to access the content from
another account, they must have API access key or a session token
provided by their authentication system.

Advantages of using GlusterFS Unified File and Object Storage
=============================================================

The following are the advantages of using GlusterFS UFO:

-   No limit on upload and download files sizes as compared to Open
    Stack Swift which limits the object size to 5GB.

-   A unified view of data across NAS and Object Storage technologies.

-   Using GlusterFS's UFO has other advantages like the following:

    -   High availability

    -   Scalability

    -   Replication

    -   Elastic Volume management

Preparing to Deploy Unified File and Object Storage
===================================================

This section provides information on pre-requisites and list of
dependencies that will be installed during the installation of Unified
File and Object Storage.

Pre-requisites
--------------

GlusterFS's Unified File and Object Storage needs `user_xattr` support
from the underlying disk file system. Use the following command to
enable `user_xattr` for GlusterFS bricks backend:

`# mount –o remount,user_xattr `

For example,

`# mount –o remount,user_xattr /dev/hda1 `

Dependencies
------------

The following packages are installed on GlusterFS when you install
Unified File and Object Storage:

-   curl

-   memcached

-   openssl

-   xfsprogs

-   python2.6

-   pyxattr

-   python-configobj

-   python-setuptools

-   python-simplejson

-   python-webob

-   python-eventlet

-   python-greenlet

-   python-pastedeploy

-   python-netifaces

Installing and Configuring Unified File and Object Storage
==========================================================

This section provides instructions on how to install and configure
Unified File and Object Storage in your storage environment.

Installing Unified File and Object Storage
------------------------------------------

To install Unified File and Object Storage:

1.  Download `rhel_install.sh` install script from [][] .

2.  Run `rhel_install.sh` script using the following command:

    `# sh rhel_install.sh`

3.  Download `swift-1.4.5-1.noarch.rpm` and
    `swift-plugin-1.0.-1.el6.noarch.rpm` files from [][].

4.  Install `swift-1.4.5-1.noarch.rpm` and
    `swift-plugin-1.0.-1.el6.noarch.rpm` using the following commands:

    `# rpm -ivh swift-1.4.5-1.noarch.rpm`

    `# rpm -ivh swift-plugin-1.0.-1.el6.noarch.rpm`

    > **Note**
    >
    > You must repeat the above steps on all the machines on which you
    > want to install Unified File and Object Storage. If you install
    > the Unified File and Object Storage on multiple servers, you can
    > use a load balancer like pound, nginx, and so on to distribute the
    > request across the machines.

Adding Users
------------

The authentication system allows the administrator to grant different
levels of access to different users based on the requirement. The
following are the types of user permissions:

-   admin user

-   normal user

Admin user has read and write permissions on the account. By default, a
normal user has no read or write permissions. A normal user can only
authenticate itself to get a Auth-Token. Read or write permission are
provided through ACLs by the admin users.

Add a new user by adding the following entry in
`/etc/swift/proxy-server.conf` file:

`user_<account-name>_<user-name> = <password> [.admin]`

For example,

`user_test_tester = testing .admin`

> **Note**
>
> During installation, the installation script adds few sample users to
> the `proxy-server.conf` file. It is highly recommended that you remove
> all the default sample user entries from the configuration file.

For more information on setting ACLs, see ?.

Configuring Proxy Server
------------------------

The Proxy Server is responsible for connecting to the rest of the
OpenStack Object Storage architecture. For each request, it looks up the
location of the account, container, or object in the ring and route the
request accordingly. The public API is also exposed through the proxy
server. When objects are streamed to or from an object server, they are
streamed directly through the proxy server to or from the user – the
proxy server does not spool them.

The configurable options pertaining to proxy server are stored in
`/etc/swift/proxy-server.conf`. The following is the sample
`proxy-server.conf` file:

    [app:proxy-server]
    use = egg:swift#proxy
    allow_account_management=true
    account_autocreate=true

    [filter:tempauth]
    use = egg:swift#tempauth                                   user_admin_admin=admin.admin.reseller_admin
    user_test_tester=testing.admin
    user_test2_tester2=testing2.admin
    user_test_tester3=testing3

    [filter:healthcheck]
    use = egg:swift#healthcheck 

    [filter:cache]
    use = egg:swift#memcache

By default, GlusterFS's Unified File and Object Storage is configured to
support HTTP protocol and uses temporary authentication to authenticate
the HTTP requests.

Configuring Authentication System
---------------------------------

Proxy server must be configured to authenticate using `
          
        `.

Configuring Proxy Server for HTTPS
----------------------------------

By default, proxy server only handles HTTP request. To configure the
proxy server to process HTTPS requests, perform the following steps:

1.  Create self-signed cert for SSL using the following commands:

        cd /etc/swift
        openssl req -new -x509 -nodes -out cert.crt -keyout cert.key

2.  Add the following lines to `/etc/swift/proxy-server.conf `under
    [DEFAULT]

        bind_port = 443
         cert_file = /etc/swift/cert.crt
         key_file = /etc/swift/cert.key

3.  Restart the servers using the following commands:

        swift-init main stop
        swift-init main start

The following are the configurable options:

  Option       Default      Description
  ------------ ------------ -------------------------------
  bind\_ip     0.0.0.0      IP Address for server to bind
  bind\_port   80           Port for server to bind
  swift\_dir   /etc/swift   Swift configuration directory
  workers      1            Number of workers to fork
  user         swift        swift user
  cert\_file                Path to the ssl .crt
  key\_file                 Path to the ssl .key

  : proxy-server.conf Default Options in the [DEFAULT] section

  Option                          Default           Description
  ------------------------------- ----------------- -----------------------------------------------------------------------------------------------------------
  use                                               paste.deploy entry point for the container server. For most cases, this should be `egg:swift#container`.
  log\_name                       proxy-server      Label used when logging
  log\_facility                   LOG\_LOCAL0       Syslog log facility
  log\_level                      INFO              Log level
  log\_headers                    True              If True, log headers in each request
  recheck\_account\_existence     60                Cache timeout in seconds to send memcached for account existence
  recheck\_container\_existence   60                Cache timeout in seconds to send memcached for container existence
  object\_chunk\_size             65536             Chunk size to read from object servers
  client\_chunk\_size             65536             Chunk size to read from clients
  memcache\_servers               127.0.0.1:11211   Comma separated list of memcached servers ip:port
  node\_timeout                   10                Request timeout to external services
  client\_timeout                 60                Timeout to read one chunk from a client
  conn\_timeout                   0.5               Connection timeout to external services
  error\_suppression\_interval    60                Time in seconds that must elapse since the last error for a node to be considered no longer error limited
  error\_suppression\_limit       10                Error count to consider a node error limited
  allow\_account\_management      false             Whether account `PUT`s and `DELETE`s are even callable

  : proxy-server.conf Server Options in the [proxy-server] section

Configuring Object Server
-------------------------

The Object Server is a very simple blob storage server that can store,
retrieve, and delete objects stored on local devices. Objects are stored
as binary files on the file system with metadata stored in the file’s
extended attributes (xattrs). This requires that the underlying file
system choice for object servers support xattrs on files.

The configurable options pertaining Object Server are stored in the file
`/etc/swift/object-server/1.conf`. The following is the sample
`object-server/1.conf` file:

    [DEFAULT]
    devices = /srv/1/node
    mount_check = false
    bind_port = 6010
    user = root
    log_facility = LOG_LOCAL2

    [pipeline:main]
    pipeline = gluster object-server

    [app:object-server]
    use = egg:swift#object 

    [filter:gluster]
    use = egg:swift#gluster

    [object-replicator]
    vm_test_mode = yes

    [object-updater]
    [object-auditor]

The following are the configurable options:

  Option         Default      Description
  -------------- ------------ ----------------------------------------------------------------------------------------------------
  swift\_dir     /etc/swift   Swift configuration directory
  devices        /srv/node    Mount parent directory where devices are mounted
  mount\_check   true         Whether or not check if the devices are mounted to prevent accidentally writing to the root device
  bind\_ip       0.0.0.0      IP Address for server to bind
  bind\_port     6000         Port for server to bind
  workers        1            Number of workers to fork

  : object-server.conf Default Options in the [DEFAULT] section

  Option                 Default         Description
  ---------------------- --------------- ----------------------------------------------------------------------------------------------------
  use                                    paste.deploy entry point for the object server. For most cases, this should be `egg:swift#object`.
  log\_name              object-server   log name used when logging
  log\_facility          LOG\_LOCAL0     Syslog log facility
  log\_level             INFO            Logging level
  log\_requests          True            Whether or not to log each request
  user                   swift           swift user
  node\_timeout          3               Request timeout to external services
  conn\_timeout          0.5             Connection timeout to external services
  network\_chunk\_size   65536           Size of chunks to read or write over the network
  disk\_chunk\_size      65536           Size of chunks to read or write to disk
  max\_upload\_time      65536           Maximum time allowed to upload an object
  slow                   0               If \> 0, Minimum time in seconds for a `PUT` or `DELETE` request to complete

  : object-server.conf Server Options in the [object-server] section

Configuring Container Server
----------------------------

The Container Server’s primary job is to handle listings of objects. The
listing is done by querying the GlusterFS mount point with path. This
query returns a list of all files and directories present under that
container.

The configurable options pertaining to container server are stored in
`/etc/swift/container-server/1.conf` file. The following is the sample
`container-server/1.conf` file:

    [DEFAULT]
    devices = /srv/1/node
    mount_check = false
    bind_port = 6011
    user = root
    log_facility = LOG_LOCAL2

    [pipeline:main]
    pipeline = gluster container-server

    [app:container-server]
    use = egg:swift#container

    [filter:gluster]
    use = egg:swift#gluster

    [container-replicator]
    [container-updater]
    [container-auditor]

The following are the configurable options:

  Option         Default      Description
  -------------- ------------ ----------------------------------------------------------------------------------------------------
  swift\_dir     /etc/swift   Swift configuration directory
  devices        /srv/node    Mount parent directory where devices are mounted
  mount\_check   true         Whether or not check if the devices are mounted to prevent accidentally writing to the root device
  bind\_ip       0.0.0.0      IP Address for server to bind
  bind\_port     6001         Port for server to bind
  workers        1            Number of workers to fork
  user           swift        Swift user

  : container-server.conf Default Options in the [DEFAULT] section

  Option          Default            Description
  --------------- ------------------ ----------------------------------------------------------------------------------------------------------
  use                                paste.deploy entry point for the container server. For most cases, this should be `egg:swift#container`.
  log\_name       container-server   Label used when logging
  log\_facility   LOG\_LOCAL0        Syslog log facility
  log\_level      INFO               Logging level
  node\_timeout   3                  Request timeout to external services
  conn\_timeout   0.5                Connection timeout to external services

  : container-server.conf Server Options in the [container-server]
  section

Configuring Account Server
--------------------------

The Account Server is very similar to the Container Server, except that
it is responsible for listing of containers rather than objects. In UFO,
each gluster volume is an account.

The configurable options pertaining to account server are stored in
`/etc/swift/account-server/1.conf` file. The following is the sample
`account-server/1.conf` file:

    [DEFAULT]
    devices = /srv/1/node
    mount_check = false
    bind_port = 6012
    user = root
    log_facility = LOG_LOCAL2

    [pipeline:main]
    pipeline = gluster account-server

    [app:account-server]
    use = egg:swift#account

    [filter:gluster]
    use = egg:swift#gluster 

    [account-replicator]
    vm_test_mode = yes

    [account-auditor]
    [account-reaper]

The following are the configurable options:

  Option         Default      Description
  -------------- ------------ ----------------------------------------------------------------------------------------------------
  swift\_dir     /etc/swift   Swift configuration directory
  devices        /srv/node    mount parent directory where devices are mounted
  mount\_check   true         Whether or not check if the devices are mounted to prevent accidentally writing to the root device
  bind\_ip       0.0.0.0      IP Address for server to bind
  bind\_port     6002         Port for server to bind
  workers        1            Number of workers to fork
  user           swift        Swift user

  : account-server.conf Default Options in the [DEFAULT] section

  Option          Default          Description
  --------------- ---------------- ----------------------------------------------------------------------------------------------------------
  use                              paste.deploy entry point for the container server. For most cases, this should be `egg:swift#container`.
  log\_name       account-server   Label used when logging
  log\_facility   LOG\_LOCAL0      Syslog log facility
  log\_level      INFO             Logging level

  : account-server.conf Server Options in the [account-server] section

Starting and Stopping Server
----------------------------

You must start the server manually when system reboots and whenever you
update/modify the configuration files.

-   To start the server, enter the following command:

    `# swift_init main start`

-   To stop the server, enter the following command:

    `# swift_init main stop`

Working with Unified File and Object Storage
============================================

This section describes the REST API for administering and managing
Object Storage. All requests will be directed to the host and URL
described in the `X-Storage-URL HTTP` header obtained during successful
authentication.

Configuring Authenticated Access
--------------------------------

Authentication is the process of proving identity to the system. To use
the REST interface, you must obtain an authorization token using GET
method and supply it with v1.0 as the path.

Each REST request against the Object Storage system requires the
addition of a specific authorization token HTTP x-header, defined as
X-Auth-Token. The storage URL and authentication token are returned in
the headers of the response.

-   To authenticate, run the following command:

        GET auth/v1.0 HTTP/1.1
        Host: <auth URL>
        X-Auth-User: <account name>:<user name>
        X-Auth-Key: <user-Password>

    For example,

        GET auth/v1.0 HTTP/1.1
        Host: auth.example.com
        X-Auth-User: test:tester
        X-Auth-Key: testing

        HTTP/1.1 200 OK
        X-Storage-Url: https:/example.storage.com:443/v1/AUTH_test
        X-Storage-Token: AUTH_tkde3ad38b087b49bbbac0494f7600a554
        X-Auth-Token: AUTH_tkde3ad38b087b49bbbac0494f7600a554
        Content-Length: 0
        Date: Wed, 10 jul 2011 06:11:51 GMT

    To authenticate access using cURL (for the above example), run the
    following command:

        curl -v -H 'X-Storage-User: test:tester' -H 'X-Storage-Pass:testing' -k
        https://auth.example.com:443/auth/v1.0

    The X-Auth-Url has to be parsed and used in the connection and
    request line of all subsequent requests to the server. In the
    example output, users connecting to server will send most
    container/object requests with a host header of example.storage.com
    and the request line's version and account as v1/AUTH\_test.

> **Note**
>
> The authentication tokens are valid for a 24 hour period.

Working with Accounts
---------------------

This section describes the list of operations you can perform at the
account level of the URL.

### Displaying Container Information

You can list the objects of a specific container, or all containers, as
needed using GET command. You can use the following optional parameters
with GET request to refine the results:

  Parameter   Description
  ----------- --------------------------------------------------------------------------
  limit       Limits the number of results to at most *n* value.
  marker      Returns object names greater in value than the specified marker.
  format      Specify either json or xml to return the respective serialized response.

**To display container information**

-   List all the containers of an account using the following command:

        GET /<apiversion>/<account> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>

    For example,

        GET /v1/AUTH_test HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

        HTTP/1.1 200 Ok
        Date: Wed, 13 Jul 2011 16:32:21 GMT
        Server: Apache
        Content-Type: text/plain; charset=UTF-8
        Content-Length: 39

        songs
        movies
        documents
        reports

To display container information using cURL (for the above example), run
the following command:

    curl -v -X GET -H 'X-Auth-Token: AUTH_tkde3ad38b087b49bbbac0494f7600a554'
    https://example.storage.com:443/v1/AUTH_test -k

### Displaying Account Metadata Information

You can issue HEAD command to the storage service to view the number of
containers and the total bytes stored in the account.

-   To display containers and storage used, run the following command:

        HEAD /<apiversion>/<account> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>

    For example,

        HEAD /v1/AUTH_test HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

        HTTP/1.1 204 No Content
        Date: Wed, 13 Jul 2011 16:52:21 GMT
        Server: Apache
        X-Account-Container-Count: 4
        X-Account-Total-Bytes-Used: 394792

    To display account metadata information using cURL (for the above
    example), run the following command:

        curl -v -X HEAD -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test -k

Working with Containers
-----------------------

This section describes the list of operations you can perform at the
container level of the URL.

### Creating Containers

You can use PUT command to create containers. Containers are the storage
folders for your data. The URL encoded name must be less than 256 bytes
and cannot contain a forward slash '/' character.

-   To create a container, run the following command:

        PUT /<apiversion>/<account>/<container>/ HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>

    For example,

        PUT /v1/AUTH_test/pictures/ HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        HTTP/1.1 201 Created

        Date: Wed, 13 Jul 2011 17:32:21 GMT
        Server: Apache
        Content-Type: text/plain; charset=UTF-8

    To create container using cURL (for the above example), run the
    following command:

        curl -v -X PUT -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/pictures -k

    The status code of 201 (Created) indicates that you have
    successfully created the container. If a container with same is
    already existed, the status code of 202 is displayed.

### Displaying Objects of a Container

You can list the objects of a container using GET command. You can use
the following optional parameters with GET request to refine the
results:

  Parameter   Description
  ----------- --------------------------------------------------------------------------------------------------------------
  limit       Limits the number of results to at most *n* value.
  marker      Returns object names greater in value than the specified marker.
  prefix      Displays the results limited to object names beginning with the substring x. beginning with the substring x.
  path        Returns the object names nested in the pseudo path.
  format      Specify either json or xml to return the respective serialized response.
  delimiter   Returns all the object names nested in the container.

To display objects of a container

-   List objects of a specific container using the following command:

<!-- -->

    GET /<apiversion>/<account>/<container>[parm=value] HTTP/1.1
    Host: <storage URL>
    X-Auth-Token: <authentication-token-key>

For example,

    GET /v1/AUTH_test/images HTTP/1.1
    Host: example.storage.com
    X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

    HTTP/1.1 200 Ok
    Date: Wed, 13 Jul 2011 15:42:21 GMT
    Server: Apache
    Content-Type: text/plain; charset=UTF-8
    Content-Length: 139

    sample file.jpg
    test-file.pdf
    You and Me.pdf
    Puddle of Mudd.mp3
    Test Reports.doc

To display objects of a container using cURL (for the above example),
run the following command:

    curl -v -X GET-H 'X-Auth-Token: AUTH_tkde3ad38b087b49bbbac0494f7600a554'
    https://example.storage.com:443/v1/AUTH_test/images -k

### Displaying Container Metadata Information

You can issue HEAD command to the storage service to view the number of
objects in a container and the total bytes of all the objects stored in
the container.

-   To display list of objects and storage used, run the following
    command:

        HEAD /<apiversion>/<account>/<container> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>

    For example,

        HEAD /v1/AUTH_test/images HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

        HTTP/1.1 204 No Content
        Date: Wed, 13 Jul 2011 19:52:21 GMT
        Server: Apache
        X-Account-Object-Count: 8
        X-Container-Bytes-Used: 472

    To display list of objects and storage used in a container using
    cURL (for the above example), run the following command:

        curl -v -X HEAD -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/images -k

### Deleting Container

You can use DELETE command to permanently delete containers. The
container must be empty before it can be deleted.

You can issue HEAD command to determine if it contains any objects.

-   To delete a container, run the following command:

        DELETE /<apiversion>/<account>/<container>/ HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>

    For example,

        DELETE /v1/AUTH_test/pictures HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

        HTTP/1.1 204 No Content
        Date: Wed, 13 Jul 2011 17:52:21 GMT
        Server: Apache
        Content-Length: 0
        Content-Type: text/plain; charset=UTF-8

    To delete a container using cURL (for the above example), run the
    following command:

        curl -v -X DELETE -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/pictures -k

    The status code of 204 (No Content) indicates that you have
    successfully deleted the container. If that container does not
    exist, the status code 404 (Not Found) is displayed, and if the
    container is not empty, the status code 409 (Conflict) is displayed.

### Updating Container Metadata

You can update the metadata of container using POST operation, metadata
keys should be prefixed with 'x-container-meta'.

-   To update the metadata of the object, run the following command:

        POST /<apiversion>/<account>/<container> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <Authentication-token-key>
        X-Container-Meta-<key>: <new value>
        X-Container-Meta-<key>: <new value>

    For example,

        POST /v1/AUTH_test/images HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        X-Container-Meta-Zoo: Lion
        X-Container-Meta-Home: Dog

        HTTP/1.1 204 No Content
        Date: Wed, 13 Jul 2011 20:52:21 GMT
        Server: Apache
        Content-Type: text/plain; charset=UTF-8

    To update the metadata of the object using cURL (for the above
    example), run the following command:

        curl -v -X POST -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/images -H ' X-Container-Meta-Zoo: Lion' -H 'X-Container-Meta-Home: Dog' -k

    The status code of 204 (No Content) indicates the container's
    metadata is updated successfully. If that object does not exist, the
    status code 404 (Not Found) is displayed.

### Setting ACLs on Container

You can set the container access control list by using POST command on
container with `x- container-read` and` x-container-write` keys.

The ACL format is `[item[,item...]]`. Each item can be a group name to
give access to or a referrer designation to grant or deny based on the
HTTP Referer header.

The referrer designation format is:` .r:[-]value`.

The .r can also be `.ref, .referer, `or .`referrer`; though it will be
shortened to.r for decreased character count usage. The value can be `*`
to specify any referrer host is allowed access. The leading minus sign
(-) indicates referrer hosts that should be denied access.

Examples of valid ACLs:

    .r:*
    .r:*,bobs_account,sues_account:sue
    bobs_account,sues_account:sue

Examples of invalid ACLs:

    .r:
    .r:-

By default, allowing read access via `r `will not allow listing objects
in the container but allows retrieving objects from the container. To
turn on listings, use the .`rlistings` directive. Also, `.r`
designations are not allowed in headers whose names include the word
write.

For example, to set all the objects access rights to "public" inside the
container using cURL (for the above example), run the following command:

    curl -v -X POST -H 'X-Auth-Token:
    AUTH_tkde3ad38b087b49bbbac0494f7600a554'
    https://example.storage.com:443/v1/AUTH_test/images
    -H 'X-Container-Read: .r:*' -k

Working with Objects
--------------------

An object represents the data and any metadata for the files stored in
the system. Through the REST interface, metadata for an object can be
included by adding custom HTTP headers to the request and the data
payload as the request body. Objects name should not exceed 1024 bytes
after URL encoding.

This section describes the list of operations you can perform at the
object level of the URL.

### Creating or Updating Object

You can use PUT command to write or update an object's content and
metadata.

You can verify the data integrity by including an MD5checksum for the
object's data in the ETag header. ETag header is optional and can be
used to ensure that the object's contents are stored successfully in the
storage system.

You can assign custom metadata to objects by including additional HTTP
headers on the PUT request. The objects created with custom metadata via
HTTP headers are identified with the`X-Object- Meta`- prefix.

-   To create or update an object, run the following command:

        PUT /<apiversion>/<account>/<container>/<object> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>
        ETag: da1e100dc9e7becc810986e37875ae38
        Content-Length: 342909
        X-Object-Meta-PIN: 2343

    For example,

        PUT /v1/AUTH_test/pictures/dog HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        ETag: da1e100dc9e7becc810986e37875ae38

        HTTP/1.1 201 Created
        Date: Wed, 13 Jul 2011 18:32:21 GMT
        Server: Apache
        ETag: da1e100dc9e7becc810986e37875ae38
        Content-Length: 0
        Content-Type: text/plain; charset=UTF-8

    To create or update an object using cURL (for the above example),
    run the following command:

        curl -v -X PUT -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/pictures/dog -H 'Content-
        Length: 0' -k

    The status code of 201 (Created) indicates that you have
    successfully created or updated the object. If there is a missing
    content-Length or Content-Type header in the request, the status
    code of 412 (Length Required) is displayed. (Optionally) If the MD5
    checksum of the data written to the storage system does not match
    the ETag value, the status code of 422 (Unprocessable Entity) is
    displayed.

#### Chunked Transfer Encoding

You can upload data without knowing the size of the data to be uploaded.
You can do this by specifying an HTTP header of Transfer-Encoding:
chunked and without using a Content-Length header.

You can use this feature while doing a DB dump, piping the output
through gzip, and then piping the data directly into Object Storage
without having to buffer the data to disk to compute the file size.

-   To create or update an object, run the following command:

        PUT /<apiversion>/<account>/<container>/<object> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <authentication-token-key>
        Transfer-Encoding: chunked
        X-Object-Meta-PIN: 2343

    For example,

        PUT /v1/AUTH_test/pictures/cat HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        Transfer-Encoding: chunked
        X-Object-Meta-PIN: 2343
        19
        A bunch of data broken up
        D
        into chunks.
        0

### Copying Object

You can copy object from one container to another or add a new object
and then add reference to designate the source of the data from another
container.

**To copy object from one container to another**

-   To add a new object and designate the source of the data from
    another container, run the following command:

        COPY /<apiversion>/<account>/<container>/<sourceobject> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: < authentication-token-key>
        Destination: /<container>/<destinationobject>

    For example,

        COPY /v1/AUTH_test/images/dogs HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        Destination: /photos/cats

        HTTP/1.1 201 Created
        Date: Wed, 13 Jul 2011 18:32:21 GMT
        Server: Apache
        Content-Length: 0
        Content-Type: text/plain; charset=UTF-8

    To copy an object using cURL (for the above example), run the
    following command:

        curl -v -X COPY -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554' -H 'Destination: /photos/cats' -k https://example.storage.com:443/v1/AUTH_test/images/dogs

    The status code of 201 (Created) indicates that you have
    successfully copied the object. If there is a missing content-Length
    or Content-Type header in the request, the status code of 412
    (Length Required) is displayed.

    You can also use PUT command to copy object by using additional
    header `X-Copy-From: container/obj`.

-   To use PUT command to copy an object, run the following command:

        PUT /v1/AUTH_test/photos/cats HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        X-Copy-From: /images/dogs

        HTTP/1.1 201 Created
        Date: Wed, 13 Jul 2011 18:32:21 GMT
        Server: Apache
        Content-Type: text/plain; charset=UTF-8

    To copy an object using cURL (for the above example), run the
    following command:

        curl -v -X PUT -H 'X-Auth-Token: AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        -H 'X-Copy-From: /images/dogs' –k
        https://example.storage.com:443/v1/AUTH_test/images/cats

    The status code of 201 (Created) indicates that you have
    successfully copied the object.

### Displaying Object Information

You can issue GET command on an object to view the object data of the
object.

-   To display the content of an object run the following command:

        GET /<apiversion>/<account>/<container>/<object> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <Authentication-token-key>

    For example,

        GET /v1/AUTH_test/images/cat HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

        HTTP/1.1 200 Ok
        Date: Wed, 13 Jul 2011 23:52:21 GMT
        Server: Apache
        Last-Modified: Thu, 14 Jul 2011 13:40:18 GMT
        ETag: 8a964ee2a5e88be344f36c22562a6486
        Content-Length: 534210
        [.........]

    To display the content of an object using cURL (for the above
    example), run the following command:

        curl -v -X GET -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/images/cat -k

    The status code of 200 (Ok) indicates the object's data is displayed
    successfully. If that object does not exist, the status code 404
    (Not Found) is displayed.

### Displaying Object Metadata

You can issue HEAD command on an object to view the object metadata and
other standard HTTP headers. You must send only authorization token as
header.

-   To display the metadata of the object, run the following command:

<!-- -->

    HEAD /<apiversion>/<account>/<container>/<object> HTTP/1.1
    Host: <storage URL>
    X-Auth-Token: <Authentication-token-key>

For example,

    HEAD /v1/AUTH_test/images/cat HTTP/1.1
    Host: example.storage.com
    X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

    HTTP/1.1 204 No Content
    Date: Wed, 13 Jul 2011 21:52:21 GMT
    Server: Apache
    Last-Modified: Thu, 14 Jul 2011 13:40:18 GMT
    ETag: 8a964ee2a5e88be344f36c22562a6486
    Content-Length: 512000
    Content-Type: text/plain; charset=UTF-8
    X-Object-Meta-House: Cat
    X-Object-Meta-Zoo: Cat
    X-Object-Meta-Home: Cat
    X-Object-Meta-Park: Cat

To display the metadata of the object using cURL (for the above
example), run the following command:

    curl -v -X HEAD -H 'X-Auth-Token:
    AUTH_tkde3ad38b087b49bbbac0494f7600a554'
    https://example.storage.com:443/v1/AUTH_test/images/cat -k

The status code of 204 (No Content) indicates the object's metadata is
displayed successfully. If that object does not exist, the status code
404 (Not Found) is displayed.

### Updating Object Metadata

You can issue POST command on an object name only to set or overwrite
arbitrary key metadata. You cannot change the object's other headers
such as Content-Type, ETag and others using POST operation. The POST
command will delete all the existing metadata and replace it with the
new arbitrary key metadata.

You must prefix **X-Object-Meta-** to the key names.

-   To update the metadata of an object, run the following command:

        POST /<apiversion>/<account>/<container>/<object> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <Authentication-token-key>
        X-Object-Meta-<key>: <new value>
        X-Object-Meta-<key>: <new value>

    For example,

        POST /v1/AUTH_test/images/cat HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554
        X-Object-Meta-Zoo: Lion
        X-Object-Meta-Home: Dog

        HTTP/1.1 202 Accepted
        Date: Wed, 13 Jul 2011 22:52:21 GMT
        Server: Apache
        Content-Length: 0
        Content-Type: text/plain; charset=UTF-8

    To update the metadata of an object using cURL (for the above
    example), run the following command:

        curl -v -X POST -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/images/cat -H ' X-Object-
        Meta-Zoo: Lion' -H 'X-Object-Meta-Home: Dog' -k

    The status code of 202 (Accepted) indicates that you have
    successfully updated the object's metadata. If that object does not
    exist, the status code 404 (Not Found) is displayed.

### Deleting Object

You can use DELETE command to permanently delete the object.

The DELETE command on an object will be processed immediately and any
subsequent operations like GET, HEAD, POST, or DELETE on the object will
display 404 (Not Found) error.

-   To delete an object, run the following command:

        DELETE /<apiversion>/<account>/<container>/<object> HTTP/1.1
        Host: <storage URL>
        X-Auth-Token: <Authentication-token-key>

    For example,

        DELETE /v1/AUTH_test/pictures/cat HTTP/1.1
        Host: example.storage.com
        X-Auth-Token: AUTH_tkd3ad38b087b49bbbac0494f7600a554

        HTTP/1.1 204 No Content
        Date: Wed, 13 Jul 2011 20:52:21 GMT
        Server: Apache
        Content-Type: text/plain; charset=UTF-8

    To delete an object using cURL (for the above example), run the
    following command:

        curl -v -X DELETE -H 'X-Auth-Token:
        AUTH_tkde3ad38b087b49bbbac0494f7600a554'
        https://example.storage.com:443/v1/AUTH_test/pictures/cat -k

    The status code of 204 (No Content) indicates that you have
    successfully deleted the object. If that object does not exist, the
    status code 404 (Not Found) is displayed.

  []: http://download.gluster.com/pub/gluster/glusterfs/3.2/UFO/
