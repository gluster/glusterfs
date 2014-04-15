# NUFA Translator

The NUFA ("Non Uniform File Access") is a variant of the DHT ("Distributed Hash
Table") translator, intended for use with workloads that have a high locality
of reference.  Instead of placing new files pseudo-randomly, it places them on
the same nodes where they are created so that future accesses can be made
locally.  For replicated volumes, this means that one copy will be local and
others will be remote; the read-replica selection mechanisms will then favor
the local copy for reads.  For non-replicated volumes, the only copy will be
local.

## Interface

Use of NUFA is controlled by a volume option, as follows.

	gluster volume set myvolume cluster.nufa on

This will cause the NUFA translator to be used wherever the DHT translator
otherwise would be.  The rest is all automatic.

