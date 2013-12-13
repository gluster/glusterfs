#Managing the glusterd Service

After installing GlusterFS, you must start glusterd service. The
glusterd service serves as the Gluster elastic volume manager,
overseeing glusterfs processes, and co-ordinating dynamic volume
operations, such as adding and removing volumes across multiple storage
servers non-disruptively.

This section describes how to start the glusterd service in the
following ways:

- [Starting and Stopping glusterd Manually](#manual)
- [Starting glusterd Automatically](#auto)

> **Note**: You must start glusterd on all GlusterFS servers.

<a name="manual" />
##Starting and Stopping glusterd Manually

This section describes how to start and stop glusterd manually

-   To start glusterd manually, enter the following command:

    `# /etc/init.d/glusterd start `

-   To stop glusterd manually, enter the following command:

    `# /etc/init.d/glusterd stop`

<a name="auto" />
##Starting glusterd Automatically

This section describes how to configure the system to automatically
start the glusterd service every time the system boots.

###Red Hat and Fedora distros

To configure Red Hat-based systems to automatically start the glusterd
service every time the system boots, enter the following from the
command line:

`# chkconfig glusterd on `

###Debian and derivatives like Ubuntu

To configure Debian-based systems to automatically start the glusterd
service every time the system boots, enter the following from the
command line:

`# update-rc.d glusterd defaults`

###Systems Other than Red Hat and Debain

To configure systems other than Red Hat or Debian to automatically start
the glusterd service every time the system boots, enter the following
entry to the*/etc/rc.local* file:

`# echo "glusterd" >> /etc/rc.local `
