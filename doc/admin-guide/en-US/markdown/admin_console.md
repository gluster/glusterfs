##Using the Gluster Console Manager – Command Line Utility

The Gluster Console Manager is a single command line utility that
simplifies configuration and management of your storage environment. The
Gluster Console Manager is similar to the LVM (Logical Volume Manager)
CLI or ZFS Command Line Interface, but across multiple storage servers.
You can use the Gluster Console Manager online, while volumes are
mounted and active. Gluster automatically synchronizes volume
configuration information across all Gluster servers.

Using the Gluster Console Manager, you can create new volumes, start
volumes, and stop volumes, as required. You can also add bricks to
volumes, remove bricks from existing volumes, as well as change
translator settings, among other operations.

You can also use the commands to create scripts for automation, as well
as use the commands as an API to allow integration with third-party
applications.

###Running the Gluster Console Manager

You can run the Gluster Console Manager on any GlusterFS server either
by invoking the commands or by running the Gluster CLI in interactive
mode. You can also use the gluster command remotely using SSH.

-   To run commands directly:

    ` # gluster peer `

    For example:

    ` # gluster peer status `

-   To run the Gluster Console Manager in interactive mode

    `# gluster`

    You can execute gluster commands from the Console Manager prompt:

    ` gluster> `

    For example, to view the status of the peer server:

    \# `gluster `

    `gluster > peer status `

    Display the status of the peer.


