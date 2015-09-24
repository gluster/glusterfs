We're using Gerrit and [Jenkins](http://jenkins-ci.org) at the moment.
Our Gerrit instance:

http://review.gluster.org

It's hosted on an ancient VM (badly needs upgrading) in some hosting
place called iWeb. We're wanting to migrate this to a Rackspace VM in
the very near future.

Our main Jenkins instance:

http://build.gluster.org

That's also a pretty-out-of-date version of Jenkins, on an badly
outdated VM. That one's in Rackspace at least. We intend on migrating to
a new VM (and new Jenkins) in the not-too-far-future. No ETA yet. ;)

As well as those two main pieces, we have a bunch of VM's in Rackspace
with various OS's on them:

http://build.gluster.org/computer/

In that list we have:

-   bulk\*.cloud.gluster.org\

    -  Temporary VM's used for running bulk regression tests on, for
        analysing our spurious regression failure problem
    -  Setup and maintained by Justin Clift

-   freebsd0.cloud.gluster.org\

    -  FreeBSD 10.0 VM in Rackspace. Used for automatic smoke testing
        on FreeBSD of all proposed patches (uses a Gerrit trigger).

-   g4s-rackspace-\* (apart from gfs-rackspace-f20-1), and
    tiny-rackspace-f20-1\

    -  Various VM's in Rackspace with Fedora and EL6 on them, setup by
    Luis Pabon. From their description in Jenkins, they're nodes for
    "open-stack swift executing functional test suite against
    Gluster-for-Swift".

-   gfs-rackspace-f20-1\

    -  A VM in Rackspace for automatically building RPMs on. Setup +
    maintained by Luis Pabon.

-   netbsd0.cloud.gluster.org\

    -  NetBSD 6.1.4 VM in Rackspace. Used for automatic smoke testing
        on NetBSD 6.x of all proposed patches (uses a Gerrit trigger).
    -  Setup and maintained by Manu Dreyfus

-   netbsd7.cloud.gluster.org\

    -  NetBSD 7 (beta) VM in Rackspace. Used for automatic smoke
        testing on NetBSD 7 of all proposed patches (uses a Gerrit
        trigger).
    -  Setup and maintained by Manu Dreyfus

-   nbslave7\*.cloud.gluster.org\

    -  NetBSD 7 slaves VMs for running our regression tests on
    -  Setup and maintained by Manu Dreyfus

-   slave20.cloud.gluster.org - slave49.cloud.gluster.org\

    -  CentOS 6.5 VM's in Rackspace. Used for automatic regression
        testing of all proposed patches (uses a Gerrit trigger).
    -  Setup and maintained by Michael Scherer

Work is being done on the GlusterFS regression tests so they'll function
on FreeBSD and NetBSD (instead of just Linux). When that's complete,
we'll automatically run full regression testing on FreeBSD and NetBSD
for all proposed patches too.

Non Jenkins VMs
---------------

**backups.cloud.gluster.org**

   Server holding our nightly backups. Setup and maintained by Michael
    Scherer.

**bareos-dev.cloud.gluster.org, bareos-data.cloud.gluster.org**

   Shared VMs to debug Bareos and libgfapi integration. Maintained by
    Niels de Vos.

**bugs.cloud.gluster.org**

   Hosting
    [gluster-bugs-webui](https://github.com/gluster/gluster-bugs-webui)
    for bug triage/checking. Maintained by Niels de Vos.

**docs.cloud.gluster.org**

   Documentation server, running readTheDocs - managed by Soumya Deb.

**download.gluster.org**

   Our primary download server - holds the Gluster binaries we
    generate, which people can download.

**gluster-sonar**

   Hosts our Gluster
    [SonarQube](http://sonar.peircean.com/dashboard/index/com.peircean.glusterfs:glusterfs-java-filesystem)
    instance. Setup and maintained by Louis Zuckerman.

**salt-master.gluster.org**

   Our Configuration Mgmt master VM. Maintained by Michael Scherer.

**munin.gluster.org**

   Munin master. Maintained by Michael Scherer.

**webbuilder.gluster.org**

   Our builder for the website. Maintained by Michael Scherer.

**www.gluster.org aka supercolony.gluster.org**

   The main website server. Maintained by Michael Scherer, Justin
    Clift, Others ( add your name )
