# How GlusterFS Distribution Works

The defining feature of any scale-out system is its ability to distribute work
or data among many servers.  Accordingly, people in the distributed-system
community have developed many powerful techniques to perform such distribution,
but those techniques often remain little known or understood even among other
members of the file system and database communities that benefit.  This
confusion is represented even in the name of the GlusterFS component that
performs distribution - DHT, which stands for Distributed Hash Table but is not
actually a DHT as that term is most commonly used or defined.  The way
GlusterFS's DHT works is based on a few basic principles:

 * All operations are driven by clients, which are all equal.  There are no
   special nodes with special knowledge of where files are or should be.
 
 * Directories exist on all subvolumes (bricks or lower-level aggregations of
   bricks); files exist on only one.
 
 * Files are assigned to subvolumes based on *consistent hashing*, and even
   more specifically a form of consistent hashing exemplified by Amazon's
   [Dynamo][dynamo].
 
The result of all this is that users are presented with a set of files that is
the union of the files present on all subvolumes.  The following sections
describe how this "uniting" process actually works.

## Layouts

The conceptual basis of Dynamo-style consistent hashing is of numbers around a
circle, like a clock.  First, the circle is divided into segments and those
segments are assigned to bricks.  (For the sake of simplicity we'll use
"bricks" hereafter even though they might actually be replicated/striped
subvolumes.)  Several factors guide this assignment.

 * Assignments are done separately for each directory.

 * Historically, segments have all been the same size.  However, this can lead
   to smaller bricks becoming full while plenty of space remains on larger
   ones.  If the *cluster.weighted-rebalance* option is set, segments sizes
   will be proportional to brick sizes.
 
 * Assignments need not include all bricks in the volume.  If the
   *cluster.subvols-per-directory* option is set, only that many bricks will
   receive assignments for that directory.
 
However these assignments are done, they collectively become what we call a
*layout* for a directory.  This layout is then stored using extended
attributes, with each brick's copy of that extended attribute on that directory
consisting of four 32-bit fields.

 * A version, which might be DHT\_HASH\_TYPE\_DM to represent an assignment as
   described above, or DHT\_HASH\_TYPE\_DM\_USER to represent an assignment made
   manually by the user (or external script).
 
 * A "commit hash" which will be described later.
 
 * The first number in the assigned range (segment).
 
 * The last number in the assigned range.
 
For example, the extended attributes representing a weighted assignment between
three bricks, one twice as big as the others, might look like this.

 * Brick A (the large one): DHT\_HASH\_TYPE\_DM 1234 0 0x7ffffff
 
 * Brick B: DHT\_HASH\_TYPE\_DM 1234 0x80000000 0xbfffffff
 
 * Brick C: DHT\_HASH\_TYPE\_DM 1234 0xc0000000 0xffffffff
 
## Placing Files

To place a file in a directory, we first need a layout for that directory - as
described above.  Next, we calculate a hash for the file.  To minimize
collisions either between files in the same directory with different names or
between files in different directories with the same name, this hash is
generated using both the (containing) directory's unique GFID and the file's
name.  This hash is then matched to one of the layout assignments, to yield
what we call a *hashed location*.  For example, consider the layout shown
above.  The hash 0xabad1dea is between 0x80000000 and 0xbfffffff, so the
corresponding file's hashed location would be on Brick B.  A second file with a
hash of 0xfaceb00c would be assigned to Brick C by the same reasoning.

## Looking Up Files

Because layout assignments might change, especially as bricks are added or
removed, finding a file involves more than calculating its hashed location and
looking there.  That is in fact the first step, and works most of the time -
i.e. the file is found where we expected it to be - but there are a few more
steps when that's not the case.  Historically, the next step has been to look
for the file **everywhere** - i.e. to broadcast our lookup request to all
subvolumes.  If the file isn't found that way, it doesn't exist.  At this
point, an open that requires the file's presence will fail, or a create/mkdir
that requires its absence will be allowed to continue.

Regardless of whether a file is found at its hashed location or elsewhere, we
now know its *cached location*.  As the name implies, this is stored within DHT
to satisfy future lookups.  If it's not the same as the hashed location, we
also take an extra step.  This step is the creation of a *linkfile*, which is a
special stub left at the **hashed** location pointing to the **cached**
location.  Therefore, if a client naively looks for a file at its hashed
location and finds a linkfile instead, it can use that linkfile to look up the
file where it really is instead of needing to inquire everywhere.

## Rebalancing

As bricks are added or removed, or files are renamed, many files can end up
somewhere other than at their hashed locations.  When this happens, the volumes
need to be rebalanced.  This process consists of two parts.

 1. Calculate new layouts, according to the current set of bricks (and possibly
 their characteristics).  We call this the "fix-layout" phase.
 
 2. Migrate any "misplaced" files to their correct (hashed) locations, and
 clean up any linkfiles which are no longer necessary.  We call this the
 "migrate-data" phase.
 
Usually, these two phases are done together.  (In fact, the code for them is
somewhat intermingled.)  However, the migrate-data phase can involve a lot of
I/O and be very disruptive, so users can do just the fix-layout phase and defer
migrate-data until a more convenient time.  This allows new files to be placed
on new bricks, even though old files might still be in the "wrong" place.

When calculating a new layout to replace an old one, DHT specifically tries to
maximize overlap of the assigned ranges, thus minimizing data movement.  This
difference can be very large.  For example, consider the case where our example
layout from earlier is updated to add a new double-sided brick.  Here's a very
inefficient way to do that.

 * Brick A (the large one): 0x00000000 to 0x55555555
 
 * Brick B: 0x55555556 to 0x7fffffff
 
 * Brick C: 0x80000000 to 0xaaaaaaaa
 
 * Brick D (the new one): 0xaaaaaaab to 0xffffffff
 
This would cause files in the following ranges to be migrated:

 * 0x55555556 to 0x7fffffff (from A to B)
 
 * 0x80000000 to 0xaaaaaaaa (from B to C)
 
 * 0xaaaaaaab to 0xbfffffff (from B to D)
 
 * 0xc0000000 to 0xffffffff (from C to D)
 
As an historical note, this is exactly what we used to do, and in this case it
would have meant moving 7/12 of all files in the volume.  Now let's consider a
new layout that's optimized to maximize overlap with the old one. 

 * Brick A: 0x00000000 to 0x55555555
 
 * Brick D: 0x55555556 to 0xaaaaaaaa  <- optimized insertion point
 
 * Brick B: 0xaaaaaaab to 0xd5555554
 
 * Brick C: 0xd5555555 to 0xffffffff
 
In this case we only need to move 5/12 of all files.  In a volume with millions
or even billions of files, reducing data movement by 1/6 of all files is a
pretty big improvement.  In the future, DHT might use "virtual node IDs" or
multiple hash rings to make rebalancing even more efficient.

## Rename Optimizations

With the file-lookup mechanisms we already have in place, it's not necessary to
move a file from one brick to another when it's renamed - even across
directories.  It will still be found, albeit a little less efficiently.  The
first client to look for it after the rename will add a linkfile, which every
other client will follow from then on.  Also, every client that has found the
file once will continue to find it based on its cached location, without any
network traffic at all.  Because the extra lookup cost is small, and the
movement cost might be very large, DHT renames the file "in place" on its
current brick instead (taking advantage of the fact that directories exist
everywhere).

This optimization is further extended to handle cases where renames are very
common.  For example, rsync and similar tools often use a "write new then
rename" idiom in which a file "xxx" is actually written as ".xxx.1234" and then
moved into place only after its contents have been fully written.  To make this
process more efficient, DHT uses a regular expression to separate the permanent
part of a file's name (in this case "xxx") from what is likely to be a
temporary part (the leading "." and trailing ".1234").  That way, after the
file is renamed it will be in its correct hashed location - which it wouldn't
be otherwise if "xxx" and ".xxx.1234" hash differently - and no linkfiles or
broadcast lookups will be necessary.

In fact, there are two regular expressions available for this purpose -
*cluster.rsync-hash-regex* and *cluster.extra-hash-regex*.  As its name
implies, *rsync-hash-regex* defaults to the pattern that regex uses, while
*extra-hash-regex* can be set by the user to support a second tool using the
same temporary-file idiom.

## Commit Hashes

A very recent addition to DHT's algorithmic arsenal is intended to reduce the
number of "broadcast" lookups the it issues.  If a volume is completely in
balance, then no file could exist anywhere but at its hashed location.
Therefore, if we've already looked there and not found it, then looking
elsewhere would be pointless (and wasteful).  The *commit hash* mechanism is
used to detect this case.  A commit hash is assigned to a volume, and
separately to each directory, and then updated according to the following
rules.

 * The volume commit hash is changed whenever actions are taken that might
   cause layout assignments across all directories to become invalid - i.e.
   bricks being added, removed, or replaced.
 
 * The directory commit hash is changed whenever actions are taken that might
   cause files to be "misplaced" - e.g. when they're renamed.
 
 * The directory commit hash is set to the volume commit hash when the
   directory is created, and whenever the directory is fully rebalanced so that
   all files are at their hashed locations.
 
In other words, whenever either the volume or directory commit hash is changed
that creates a mismatch.  In that case we revert to the "pessimistic"
broadcast-lookup method described earlier.  However, if the two hashes match
then we can with skip the broadcast lookup and return a result immediately.
This has been observed to cause a 3x performance improvement in workloads that
involve creating many small files across many bricks.

[dynamo]: http://www.allthingsdistributed.com/files/amazon-dynamo-sosp2007.pdf
