#Managing Hadoop Compatible Storage

GlusterFS provides compatibility for Apache Hadoop and it uses the
standard file system APIs available in Hadoop to provide a new storage
option for Hadoop deployments. Existing MapReduce based applications can
use GlusterFS seamlessly. This new functionality opens up data within
Hadoop deployments to any file-based or object-based application.

##Advantages

The following are the advantages of Hadoop Compatible Storage with
GlusterFS:

-   Provides simultaneous file-based and object-based access within
    Hadoop.
-   Eliminates the centralized metadata server.
-   Provides compatibility with MapReduce applications and rewrite is
    not required.
-   Provides a fault tolerant file system.

###Pre-requisites

The following are the pre-requisites to install Hadoop Compatible
Storage :

-   Java Runtime Environment
-   getfattr - command line utility

##Installing, and Configuring Hadoop Compatible Storage

See the detailed instruction set at https://forge.gluster.org/hadoop/pages/ConfiguringHadoop2
