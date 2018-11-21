#Iobuf-pool
##Datastructures
###iobuf
Short for IO Buffer. It is one allocatable unit for the consumers of the IOBUF
API, each unit hosts @page_size bytes of memory. As initial step of processing
a fop, the IO buffer passed onto GlusterFS by the other applications (FUSE VFS/
Applications using gfapi) is copied into GlusterFS space i.e. iobufs. Hence Iobufs
are mostly allocated/deallocated in Fuse, gfapi, protocol xlators, and also in
performance xlators to cache the IO buffers etc.

Iobufs is allocated from the per thread mem pool. 

###iobref
There may be need of multiple iobufs for a single fop, like in vectored read/write.
Hence multiple iobufs(default 16) are encapsulated under one iobref.
```
struct iobref {
        gf_lock_t          lock;
        int                ref;
        struct iobuf     **iobrefs; /* list of iobufs */
        int                alloced; /* 16 by default, grows as required */
        int                used;    /* number of iobufs added to this iobref */
};
```
###iobuf_pool
This is just a wrapper structure to keep count of active iobufs, iobuf mem pool
alloc misses and hits.

##APIs
###iobuf_get

```
struct iobuf *iobuf_get (struct iobuf_pool *iobuf_pool);
```
Creates a new iobuf of the default page size(128KB hard coded as of yet).
Also takes a reference(increments ref count), hence no need of doing it
explicitly after getting iobuf.

###iobuf_get2

```
struct iobuf * iobuf_get2 (struct iobuf_pool *iobuf_pool, size_t page_size);
```
Creates a new iobuf of a specified page size, if page_size=0 default page size
is considered.
```
if (requested iobuf size > Max size in the mem pool(1MB as of yet))
  {
     Perform standard allocation(CALLOC) of the requested size
  }
  else
  {
     -request for memory from the per thread mem pool. This can be a miss
     or hit, based on the availablility in the mem pool. Record the hit/miss
     in the iobuf_pool.
  }
```
Also takes a reference(increments ref count), hence no need of doing it
explicitly after getting iobuf.

###iobuf_ref

```
struct iobuf *iobuf_ref (struct iobuf *iobuf);
```
  Take a reference on the iobuf. If using an iobuf allocated by some other
xlator/function/, its a good practice to take a reference so that iobuf is not
deleted by the allocator.

###iobuf_unref
```
void iobuf_unref (struct iobuf *iobuf);
```
Unreference the iobuf, if the ref count is zero iobuf is considered free.

```
  -Delete the iobuf, if allocated from standard alloc and return.  
  -set the active/passive count appropriately.  
```
Every iobuf_ref should have a corresponding iobuf_unref, and also every
iobuf_get/2 should have a correspondning iobuf_unref.

###iobref_new
```
struct iobref *iobref_new ();
```
Creates a new iobref structure and returns its pointer.

###iobref_ref
```
struct iobref *iobref_ref (struct iobref *iobref);
```
Take a reference on the iobref.

###iobref_unref
```
void iobref_unref (struct iobref *iobref);
```
Decrements the reference count of the iobref. If the ref count is 0, then unref
all the iobufs(iobuf_unref) in the iobref, and destroy the iobref.

###iobref_add
```
int iobref_add (struct iobref *iobref, struct iobuf *iobuf);
```
Adds the given iobuf into the iobref, it takes a ref on the iobuf before adding
it, hence explicit iobuf_ref is not required if adding to the iobref.

###iobref_merge
```
int iobref_merge (struct iobref *to, struct iobref *from);
```
Adds all the iobufs in the 'from' iobref to the 'to' iobref. Merge will not
cause the delete of the 'from' iobref, therefore it will result in another ref
on all the iobufs added to the 'to' iobref. Hence iobref_unref should be
performed both on 'from' and 'to' iobrefs (performing iobref_unref only on 'to'
will not free the iobufs and may result in leak).

###iobref_clear
```
void iobref_clear (struct iobref *iobref);
```
Unreference all the iobufs in the iobref, and also unref the iobref.

##Iobuf Leaks
If all iobuf_refs/iobuf_new do not have correspondning iobuf_unref, then the
iobufs are not freed and recurring execution of such code path may lead to huge
memory leaks. The easiest way to identify if a memory leak is caused by iobufs
is to take a statedump.

If iobufs are leaking, the next step is to find where the iobuf_unref went
missing. There is no standard/easy way of debugging this, code reading and logs
are the only ways. If there is a liberty to reproduce the memory leak at will,
then logs(gf_callinginfo) in iobuf_ref/unref might help.  
TODO: A easier way to debug iobuf leaks.
