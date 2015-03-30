## Background


For a more detailed description, view Jeff Darcy's blog post [here] 
(http://pl.atyp.us/hekafs.org/index.php/2012/03/glusterfs-algorithms-distribution/)

GlusterFS uses the distribute translator (DHT) to aggregate space of multiple servers. DHT distributes files among its subvolumes using a consistent hashing method providing 32-bit hashes. Each DHT subvolume is given a range in the 32-bit hash space. A hash value is calculated for every file using a combination of its name. The file is then placed in the subvolume with the hash range that contains the hash value. 

## What is rebalance?

The rebalance process migrates files between the DHT subvolumes when necessary.

## When is rebalance required?

Rebalancing is required for two main cases.

1. Addition/Removal of bricks

2. Renaming of a file 

## Addition/Removal of  bricks

Whenever the number or order of DHT subvolumes change, the hash range given to each subvolume is recalculated. When this happens, already existing files on the volume will need to be moved to the correct subvolume based on their hash. Rebalance does this activity.

Addition of bricks which increase the size of a volume will increase the number of DHT subvolumes and lead to recalculation of hash ranges (This doesn't happen when bricks are added to a volume to increase redundancy, i.e. increase replica count of a volume). This will require an explicit rebalance command to be issued to migrate the files.

Removal of bricks which decrease the size of a volumes also causes the hash ranges of DHT to be recalculated. But we don't need to issue an explicit rebalance command in this case, as rebalance is done automatically by the remove-brick process if needed.

## Renaming of a file

Renaming of file will cause its hash to change. The file now needs to be moved to the correct subvolume based on its new hash. Rebalance does this.

## How does rebalance work?

At a high level, the rebalance process consists of the following 3 steps:

1. Crawl the volume to access all files
2. Calculate the hash for the file
3. If needed move the migrate the file to the correct subvolume.


The rebalance process has been optimized by making it distributed across the trusted storage pool. With distributed rebalance, a rebalance process is launched on each peer in the cluster. Each rebalance process will crawl files on only those bricks of the volume which are present on it, and migrate the files which need migration to the correct brick. This speeds up the rebalance process considerably.

## What will happen if rebalance is not run?

### Addition of bricks

With the current implementation of add-brick, when the size of a volume is augmented by adding new bricks, the new bricks are not put into use immediately i.e., the hash ranges there not recalculated immediately. This means that the files will still be placed only onto the existing bricks, leaving the newly added storage space unused. Starting a rebalance process on the volume will cause the hash ranges to be recalculated with the new bricks included, which allows the newly added storage space to be used.

### Renaming a file

When a file rename causes the file to be hashed to a new subvolume, DHT writes a link file on the new subvolume leaving the actual file on the original subvolume. A link file is an empty file, which has an extended attribute set that points to the subvolume on which the actual file exists. So, when a client accesses the renamed file, DHT first looks for the file in the hashed subvolume and gets the link file. DHT understands the link file, and gets the actual file from the subvolume pointed to by the link file. This leads to a slight reduction in performance. A rebalance will move the actual file to the hashed subvolume, allowing clients to access the file directly once again.

## Are clients affected during a rebalance process?

The rebalance process is transparent to applications on the clients. Applications which have open files on the volume will not be affected by the rebalance process, even if the open file requires migration. The DHT translator on the client will hide the migration from the applications.

##How are open files migrated?

(A more technical description of the algorithm used can be seen in the commit message of commit a07bb18c8adeb8597f62095c5d1361c5bad01f09.)

To achieve migration of open files, two things need to be assured of,
a) any writes or changes happening to the file during migration are correctly synced to destination subvolume after the migration is complete.
b) any further changes should be made to the destination subvolume

Both of these requirements require sending notificatoins to clients. Clients are notified by overloading an attribute used in every callback functions. DHT understands these attributes in the callbacks and can be notified if a file is being migrated or not.

During rebalance, a file will be in two phases

1. Migration in process - In this phase the file is being migrated by the rebalance process from the source subvolume to the destination subvolume. The rebalance process will set a 'in-migration' attribute on the file, which will notify the clients' DHT translator. The clients' DHT translator will then take care to send any further changes to the destination subvolume as well. This way we satisfy the first requirement

2. Migration completed - Once the file has been migrated, the rebalance process will set a 'migration-complete' attribute on the file. The clients will be notified of the completion and all further operations on the file will happen on the destination subvolume.

The DHT translator handles the above and allows the applications on the clients to continue working on a file under migration.
