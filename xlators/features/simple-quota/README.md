# simple-quota

It provides feature of managing blocks used below a given namespace. (Expects the inodes to be linked to the proper namespace). It follows a simple logic for quota implementation: I manage only the local data, but if someone 'trusted' tells me, there is more usage in other places, I will consider it. I don't preserve the information about cluster view, so, everytime I am restarted, someone should give me global data to be proper.

This translator is designed to be sitting on brick side (ie, somewhere above `storage/posix` in same graph).

More on the reasons for this translator is given in [this RFC](https://kadalu.io/rfcs/0006-optimized-quota-feature-with-namespace.html)

## Usage

### Set Quota

One need to mark a directory as namespace first, and then set Quota on top of it.

`setfattr -n trusted.glusterfs.namespace -v true ${mountpoint}/directory`

To set 100MiB quota limit:

`setfattr -n trusted.gfs.squota.limit -v 100000000 ${mountpoint}/directory`


### Updating Quota

Updating the hard limit is as simple as calling above xattr again.

`setfattr -n trusted.gfs.squota.limit -v 500000000 ${mountpoint}/directory`


### Deleting the Quota

Call removexattr() on the directory, with the flag.

`setfattr -x trusted.gfs.squota.limit ${mountpoint}/directory`


## Helper process

This quota feature is not complete without a global view helper process, which sees the complete data from all bricks.

Idea is to run it at regular frequency on all the directories which has Quota limit set


```
...
qdir=${mountpoint}/directory

used_size=$(df --block-size=1 --output=used $qdir | tail -n1);
setfattr -n glusterfs.quota.total-usage -v ${used_size} $qdir;

...

```

With this, the total usage would be updated in the translator and the new value would be considered for quota checks.
