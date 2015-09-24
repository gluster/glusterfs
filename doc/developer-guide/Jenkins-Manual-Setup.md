Setting up Jenkins slaves on Rackspace for GlusterFS regression testing
=======================================================================

This is for RHEL/CentOS 6.x. The below commands should be run as root.

### Install additional required packages

		yum -y install cmockery2-devel dbench libacl-devel mock nfs-utils yajl perl-Test-Harness salt-minion

### Enable yum-cron for automatic rpm updates

		chkconfig yum-cron on

### Add the mock user

		useradd -g mock mock

### Disable eth1

Because GlusterFS can fail if more than 1 ethernet interface

		sed -i 's/ONBOOT=yes/ONBOOT=no/' /etc/sysconfig/network-scripts/ifcfg-eth1

### Disable IPv6

As per <https://access.redhat.com/site/node/8709>

		sed -i 's/IPV6INIT=yes/IPV6INIT=no/' /etc/sysconfig/network-scripts/ifcfg-eth0
		echo 'options ipv6 disable=1' > /etc/modprobe.d/ipv6.conf
		chkconfig ip6tables off
		sed -i 's/NETWORKING_IPV6=yes/NETWORKING_IPV6=no/' /etc/sysconfig/network
		echo ' ' >> /etc/sysctl.conf
		echo '# ipv6 support in the kernel, set to 0 by default' >> /etc/sysctl.conf
		echo 'net.ipv6.conf.all.disable_ipv6 = 1' >> /etc/sysctl.conf
		echo 'net.ipv6.conf.default.disable_ipv6 = 1' >> /etc/sysctl.conf
		sed -i 's/v     inet6/-     inet6/' /etc/netconfig

### Update hostname

		vi /etc/sysconfig/network
		vi /etc/hosts

### Remove IPv6 and eth1 interface from /etc/hosts

		sed -i 's/^10\./#10\./' /etc/hosts
		sed -i 's/^2001/#2001/' /etc/hosts

### Install ntp

		yum -y install ntp
		chkconfig ntpdate on
		service ntpdate start

### Install OpenJDK, needed for Jenkins slaves

		yum -y install java-1.7.0-openjdk

### Create the Jenkins user

		useradd -G wheel jenkins
		chmod 755 /home/jenkins

### Set the Jenkins password

		passwd jenkins

### Copy the Jenkins SSH key from build.gluster.org

		mkdir /home/jenkins/.ssh
		chmod 700 /home/jenkins/.ssh
		cp `<somewhere>` /home/jenkins/.ssh/id_rsa
		chown -R jenkins:jenkins /home/jenkins/.ssh
		chmod 600 /home/jenkins/.ssh/id_rsa

### Generate the SSH known hosts file for jenkins user

		su - jenkins
		mkdir ~/foo
		cd ~/foo
		git clone `[`ssh://build@review.gluster.org/glusterfs.git`](ssh://build@review.gluster.org/glusterfs.git)
		(this will ask if the new host fingerprint should be added.  Choose yes)
		cd ..
		rm -rf ~/foo
		 exit

### Install git from RPMForge

		yum -y install http://pkgs.repoforge.org/rpmforge-release/rpmforge-release-0.5.3-1.el6.rf.x86_64.rpm
		yum -y --enablerepo=rpmforge-extras update git

### Install the GlusterFS patch acceptance tests

		git clone git://forge.gluster.org/gluster-patch-acceptance-tests/gluster-patch-acceptance-tests.git /opt/qa

### Add the loopback mount point to /etc/fstab

For the 1GB Rackspace VM's use this:

		echo '/backingstore           /d                      xfs     loop            0 2' >> /etc/fstab
		mount /d

For the 2GB and above Rackspace VM's use this:

		echo '/dev/xvde   /d   xfs   defaults   0 2' >> /etc/fstab
		mount /d

### Create the directories needed for the regression testing

		JDIRS="/var/log/glusterfs /var/lib/glusterd /var/run/gluster /d /d/archived_builds /d/backends /d/build /d/logs /home/jenkins/root"
		mkdir -p $JDIRS
		chown jenkins:jenkins $JDIRS
		chmod 755 $JDIRS
		ln -s /d/build /build

### Create the directories where regression logs are archived

		ADIRS="/archives/archived_builds /archives/logs"
		mkdir -p $ADIRS
		chown jenkins:jenkins $ADIRS
		chmod 755 $ADIRS

### Install Nginx

For making logs available over http

		yum -y install http://nginx.org/packages/centos/6/noarch/RPMS/nginx-release-centos-6-0.el6.ngx.noarch.rpm
		yum -y install nginx
		lokkit -s http

### Copy the Nginx config file into place

		cp -f /opt/qa/nginx/default.conf /etc/nginx/conf.d/default.conf

### Enable wheel group for sudo

		sed -i 's/# %wheel\tALL=(ALL)\tNOPASSWD/%wheel\tALL=(ALL)\tNOPASSWD/' /etc/sudoers

### Reboot (for networking changes to take effect)

		reboot

### Add forward and reverse DNS entries for the slave into Rackspace DNS

Rackspace recently added [API calls for its Cloud
DNS](https://developer.rackspace.com/docs/cloud-dns/getting-started/?lang=python)
service, so we should be able to fully automate this part as well now.