# DHT2 Experimental README

DHT2 is the new distribution scheme being developed for Gluster, that
aims to remove the subdirectory spread across all DHT subvolumes.

As a result of this work, the Gluster backend file layouts and on disk
representation of directories and files are modified, thus making DHT2
volumes incompatible to existing DHT based Gluster deployments.

This document presents interested users with relevant data to play around
with DHT2 volumes and provide feedback towards the same.

REMOVEME: Design details currently under review here,
	- http://review.gluster.org/#/c/13395/

TODO: Add more information as relevant code is pulled in

# Directory strucutre elaborated

## dht2-server
This directory contains code for the server side DHT2 xlator. This xlator is
intended to run on the brick graph, and is responsible for FOP synchronization,
redirection, transactions, and journal replays.

NOTE: The server side code also handles changes to volume/cluster map and
also any rebalance activities.

## dht2-client
This directory contains code for the client side DHT2 xlator. This xlator is
intended to run on the client/access protocol/mount graph, and is responsible
for FOP routing to the right DHT2 subvolume. It uses a volume/cluster wide map
of the routing (layout), to achieve the same.

## dht2-common
This directory contains code that is used in common across other parts of DHT2.
For example, FOP routing store/consult abstractions that are common across the
client and server side of DHT2.

## Issue: How to build dht2-common?
  1. Build a shared object
    - We cannot ship this as a part of both the client xlator RPM
  2. Build an archive
    - Symbol clashes? when both the client and server xlators are loaded as a
    part of the same graph
  3. Compile with other parts of the code that needs it 
    - Not a very different from (2) above
    - This is what is chosen at present, and maybe would be revised later
