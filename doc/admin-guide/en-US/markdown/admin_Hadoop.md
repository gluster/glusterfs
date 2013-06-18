Managing Hadoop Compatible Storage
==================================

GlusterFS provides compatibility for Apache Hadoop and it uses the
standard file system APIs available in Hadoop to provide a new storage
option for Hadoop deployments. Existing MapReduce based applications can
use GlusterFS seamlessly. This new functionality opens up data within
Hadoop deployments to any file-based or object-based application.

Architecture Overview
=====================

The following diagram illustrates Hadoop integration with GlusterFS:

Advantages
==========

The following are the advantages of Hadoop Compatible Storage with
GlusterFS:

-   Provides simultaneous file-based and object-based access within
    Hadoop.

-   Eliminates the centralized metadata server.

-   Provides compatibility with MapReduce applications and rewrite is
    not required.

-   Provides a fault tolerant file system.

Preparing to Install Hadoop Compatible Storage
==============================================

This section provides information on pre-requisites and list of
dependencies that will be installed during installation of Hadoop
compatible storage.

Pre-requisites
--------------

The following are the pre-requisites to install Hadoop Compatible
Storage :

-   Hadoop 0.20.2 is installed, configured, and is running on all the
    machines in the cluster.

-   Java Runtime Environment

-   Maven (mandatory only if you are building the plugin from the
    source)

-   JDK (mandatory only if you are building the plugin from the source)

-   getfattr - command line utility

Installing, and Configuring Hadoop Compatible Storage
=====================================================

This section describes how to install and configure Hadoop Compatible
Storage in your storage environment and verify that it is functioning
correctly.

1.  Download `glusterfs-hadoop-0.20.2-0.1.x86_64.rpm` file to each
    server on your cluster. You can download the file from [][].

2.  To install Hadoop Compatible Storage on all servers in your cluster,
    run the following command:

    `# rpm –ivh --nodeps glusterfs-hadoop-0.20.2-0.1.x86_64.rpm`

    The following files will be extracted:

    -   /usr/local/lib/glusterfs-Hadoop-version-gluster\_plugin\_version.jar

    -   /usr/local/lib/conf/core-site.xml

3.  (Optional) To install Hadoop Compatible Storage in a different
    location, run the following command:

    `# rpm –ivh --nodeps –prefix /usr/local/glusterfs/hadoop glusterfs-hadoop- 0.20.2-0.1.x86_64.rpm`

4.  Edit the `conf/core-site.xml` file. The following is the sample
    `conf/core-site.xml` file:

        <configuration>
          <property>
            <name>fs.glusterfs.impl</name>
            <value>org.apache.hadoop.fs.glusterfs.Gluster FileSystem</value>
        </property>

        <property>
           <name>fs.default.name</name>
           <value>glusterfs://fedora1:9000</value>
        </property>

        <property>
           <name>fs.glusterfs.volname</name>
           <value>hadoopvol</value>
        </property>  
         
        <property>
           <name>fs.glusterfs.mount</name>
           <value>/mnt/glusterfs</value>
        </property>

        <property>
           <name>fs.glusterfs.server</name>
           <value>fedora2</value>
        </property>

        <property>
           <name>quick.slave.io</name>
           <value>Off</value>
        </property>
        </configuration>

    The following are the configurable fields:

      -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
      Property Name          Default Value              Description
      ---------------------- -------------------------- ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
      fs.default.name        glusterfs://fedora1:9000   Any hostname in the cluster as the server and any port number.

      fs.glusterfs.volname   hadoopvol                  GlusterFS volume to mount.

      fs.glusterfs.mount     /mnt/glusterfs             The directory used to fuse mount the volume.

      fs.glusterfs.server    fedora2                    Any hostname or IP address on the cluster except the client/master.

      quick.slave.io         Off                        Performance tunable option. If this option is set to On, the plugin will try to perform I/O directly from the disk file system (like ext3 or ext4) the file resides on. Hence read performance will improve and job would run faster.
                                                        > **Note**
                                                        >
                                                        > This option is not tested widely
      -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

5.  Create a soft link in Hadoop’s library and configuration directory
    for the downloaded files (in Step 3) using the following commands:

    `# ln -s >`

    For example,

    `# ln –s /usr/local/lib/glusterfs-0.20.2-0.1.jar /lib/glusterfs-0.20.2-0.1.jar`

    `# ln –s /usr/local/lib/conf/core-site.xml /conf/core-site.xml `

6.  (Optional) You can run the following command on Hadoop master to
    build the plugin and deploy it along with core-site.xml file,
    instead of repeating the above steps:

    `# build-deploy-jar.py -d  -c `

Starting and Stopping the Hadoop MapReduce Daemon
=================================================

To start and stop MapReduce daemon

-   To start MapReduce daemon manually, enter the following command:

    `# /bin/start-mapred.sh`

-   To stop MapReduce daemon manually, enter the following command:

    `# /bin/stop-mapred.sh `

> **Note**
>
> You must start Hadoop MapReduce daemon on all servers.

  []: http://download.gluster.com/pub/gluster/glusterfs/qa-releases/3.3-beta-2/glusterfs-hadoop-0.20.2-0.1.x86_64.rpm
