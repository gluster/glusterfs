#Iobuf-pool
##Datastructures
###iobuf
Short for IO Buffer. It is one allocatable unit for the consumers of the IOBUF
API, each unit hosts @page_size(defined in arena structure) bytes of memory. As
initial step of processing a fop, the IO buffer passed onto GlusterFS by the
other applications (FUSE VFS/ Applications using gfapi) is copied into GlusterFS
space i.e. iobufs. Hence Iobufs are mostly allocated/deallocated in Fuse, gfapi,
protocol xlators, and also in performance xlators to cache the IO buffers etc.
```
struct iobuf {
        union {
                struct list_head      list;
                struct {
                        struct iobuf *next;
                        struct iobuf *prev;
                };
        };
        struct iobuf_arena  *iobuf_arena;

        gf_lock_t            lock; /* for ->ptr and ->ref */
        int                  ref;  /* 0 == passive, >0 == active */

        void                *ptr;  /* usable memory region by the consumer */

        void                *free_ptr; /* in case of stdalloc, this is the
                                          one to be freed not the *ptr */
};
```

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
###iobuf_arenas
One region of memory MMAPed from the operating system. Each region MMAPs
@arena_size bytes of memory, and hosts @arena_size / @page_size IOBUFs.
The same sized iobufs are grouped into one arena, for sanity of access.

```
struct iobuf_arena {
        union {
                struct list_head            list;
                struct {
                        struct iobuf_arena *next;
                        struct iobuf_arena *prev;
                };
        };

        size_t              page_size;  /* size of all iobufs in this arena */
        size_t              arena_size; /* this is equal to
                                           (iobuf_pool->arena_size / page_size)
                                           * page_size */
        size_t              page_count;

        struct iobuf_pool  *iobuf_pool;

        void               *mem_base;
        struct iobuf       *iobufs;     /* allocated iobufs list */

        int                 active_cnt;
        struct iobuf        active;     /* head node iobuf
                                           (unused by itself) */
        int                 passive_cnt;
        struct iobuf        passive;    /* head node iobuf
                                           (unused by itself) */
        uint64_t            alloc_cnt;  /* total allocs in this pool */
        int                 max_active; /* max active buffers at a given time */
};

```
###iobuf_pool
Pool of Iobufs. As there may be many Io buffers required by the filesystem,
a pool of iobufs are preallocated and kept, if these preallocated ones are
exhausted only then the standard malloc/free is called, thus improving the
performance. Iobuf pool is generally one per process, allocated during
glusterfs_ctx_t init (glusterfs_ctx_defaults_init), currently the preallocated
iobuf pool memory is freed on process exit. Iobuf pool is globally accessible
across GlusterFs, hence iobufs allocated by any xlator can be accessed by any
other xlators(unless iobuf is not passed).
```
struct iobuf_pool {
        pthread_mutex_t     mutex;
        size_t              arena_size; /* size of memory region in
                                           arena */
        size_t              default_page_size; /* default size of iobuf */

        int                 arena_cnt;
        struct list_head    arenas[GF_VARIABLE_IOBUF_COUNT];
        /* array of arenas. Each element of the array is a list of arenas
           holding iobufs of particular page_size */

        struct list_head    filled[GF_VARIABLE_IOBUF_COUNT];
        /* array of arenas without free iobufs */

        struct list_head    purge[GF_VARIABLE_IOBUF_COUNT];
        /* array of of arenas which can be purged */

        uint64_t            request_misses; /* mostly the requests for higher
                                               value of iobufs */
};
```
~~~
The default size of the iobuf_pool(as of yet):
1024 iobufs of 128Bytes = 128KB
512  iobufs of 512Bytes = 256KB
512  iobufs of 2KB      = 1MB
128  iobufs of 8KB      = 1MB
64   iobufs of 32KB     = 2MB
32   iobufs of 128KB    = 4MB
8    iobufs of 256KB    = 2MB
2    iobufs of 1MB      = 2MB
Total ~13MB
~~~
As seen in the datastructure iobuf_pool has 3 arena lists.

- arenas:  
The arenas allocated during iobuf_pool create, are part of this list. This list
also contains arenas that are partially filled i.e. contain few active and few
passive iobufs (passive_cnt !=0, active_cnt!=0 except for initially allocated
arenas). There will be by default 8 arenas of the sizes mentioned above.
- filled:  
If all the iobufs in the arena are filled(passive_cnt = 0), the arena is moved
to the filled list. If any of the iobufs from the filled arena is iobuf_put,
then the arena moves back to the 'arenas' list.
- purge:  
If there are no active iobufs in the arena(active_cnt = 0), the arena is moved
to purge list. iobuf_put() triggers destruction of the arenas in this list. The
arenas in the purge list are destroyed only if there is  atleast one arena in
'arenas' list, that way there won't be spurious mmap/unmap of buffers.
(e.g: If there is an arena (page_size=128KB, count=32) in purge list, this arena
is destroyed(munmap) only if there is an arena in 'arenas' list with page_size=128KB).

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
if (requested iobuf size > Max iobuf size in the pool(1MB as of yet))
  {
     Perform standard allocation(CALLOC) of the requested size and
     add it to the list iobuf_pool->arenas[IOBUF_ARENA_MAX_INDEX].
  }
  else
  {
     -Round the page size to match the stndard sizes in iobuf pool.
     (eg: if 3KB is requested, it is rounded to 8KB).
     -Select the arena list corresponding to the rounded size
     (eg: select 8KB arena)
     If the selected arena has passive count > 0, then return the
     iobuf from this arena, set the counters(passive/active/etc.)
     appropriately.
     else the arena is full, allocate new arena with rounded size
     and standard page numbers and add to the arena list
     (eg: 128 iobufs of 8KB is allocated).
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
  -if passive count > 0 then add the arena to 'arena' list.  
  -if active count = 0 then add the arena to 'purge' list.  
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
is to take a statedump. If the statedump shows a lot of filled arenas then it is
a sure sign of leak. Refer doc/debugging/statedump.md for more details.

If iobufs are leaking, the next step is to find where the iobuf_unref went
missing. There is no standard/easy way of debugging this, code reading and logs
are the only ways. If there is a liberty to reproduce the memory leak at will,
then logs(gf_callinginfo) in iobuf_ref/unref might help.  
TODO: A easier way to debug iobuf leaks.
