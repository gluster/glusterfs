# Overview of Ganesha HA Resource Agents in GlusterFS 3.7

The ganesha_mon RA monitors its ganesha.nfsd daemon. While the
daemon is running, it creates two attributes: ganesha-active and
grace-active. When the daemon stops for any reason, the attributes
are deleted. Deleting the ganesha-active attribute triggers the
failover of the virtual IP (the IPaddr RA) to another node —
according to constraint location rules — where ganesha.nfsd is
still running.

The ganesha_grace RA monitors the grace-active attribute. When
the grace-active attibute is deleted, the ganesha_grace RA stops,
and will not restart. This triggers pacemaker to invoke the notify
action in the ganesha_grace RAs on the other nodes in the cluster;
which send a DBUS message to their respective ganesha.nfsd.

(N.B. grace-active is a bit of a misnomer. while the grace-active
attribute exists, everything is normal and healthy. Deleting the
attribute triggers putting the surviving ganesha.nfsds into GRACE.)

To ensure that the remaining/surviving ganesha.nfsds are put into
 NFS-GRACE before the IPaddr (virtual IP) fails over there is a
short delay (sleep) between deleting the grace-active attribute
and the ganesha-active attribute. To summarize, e.g. in a four
node cluster:

1. on node 2 ganesha_mon::monitor notices that ganesha.nfsd has died

2. on node 2 ganesha_mon::monitor deletes its grace-active attribute

3. on node 2 ganesha_grace::monitor notices that grace-active is gone
and returns OCF_ERR_GENERIC, a.k.a. new error. When pacemaker tries
to (re)start ganesha_grace, its start action will return
OCF_NOT_RUNNING, a.k.a. known error, don't attempt further restarts.

4. on nodes 1, 3, and 4, ganesha_grace::notify receives a post-stop
notification indicating that node 2 is gone, and sends a DBUS message
to its ganesha.nfsd, putting it into NFS-GRACE.

5. on node 2 ganesha_mon::monitor waits a short period, then deletes
its ganesha-active attribute. This triggers the IPaddr (virt IP)
failover according to constraint location rules.

