#Mem-pool
##Background
There was a time when every fop in glusterfs used to incur cost of allocations/de-allocations for every stack wind/unwind between xlators because stack/frame/*_localt_t in every wind/unwind was allocated and de-allocated. Because of all these system calls in the fop path there was lot of latency and the worst part is that most of the times the number of frames/stacks active at any time wouldn't cross a threshold. So it was decided that this threshold number of frames/stacks would be allocated in the beginning of the process only once. Get one of them from the pool of stacks/frames whenever `STACK_WIND` is performed and put it back into the pool in `STACK_UNWIND`/`STACK_DESTROY` without incurring any extra system calls. The data structures are allocated only when threshold number of such items are in active use i.e. pool is in complete use.% increase in the performance once this was added to all the common data structures (inode/fd/dict etc) in xlators throughout the stack was tremendous.

## Data structure
```
struct mem_pool {
        struct list_head  list; /*Each member in the mempool is element padded with a doubly-linked-list + ptr of mempool + is-in
-use info. This list is used to add the element to the list of free members in the mem-pool*/
        int               hot_count;/*number of mempool elements that are in active use*/
        int               cold_count;/*number of mempool elements that are not in use. If a new allocation is required it
will be served from here until all the elements in the pool are in use i.e. cold-count becomes 0.*/
        gf_lock_t         lock;/*synchronization mechanism*/
        unsigned long     padded_sizeof_type;/*Each mempool element is padded with a doubly-linked-list + ptr of mempool + is-in
-use info to operate the pool of elements, this size is the element-size after padding*/
        void             *pool;/*Starting address of pool*/
        void             *pool_end;/*Ending address of pool*/
/* If an element address is in the range between pool, pool_end addresses  then it is alloced from the pool otherwise it is 'calloced' this is very useful for functions like 'mem_put'*/
        int               real_sizeof_type;/* size of just the element without any padding*/
        uint64_t          alloc_count; /*Number of times this type of data is allocated through out the life of this process. This may include calloced elements as well*/
        uint64_t          pool_misses; /*Number of times the element had to be allocated from heap because all elements from the pool are in active use.*/
        int               max_alloc; /*Maximum number of elements from the pool in active use at any point in the life of the process. This does *not* include calloced elements*/
        int               curr_stdalloc;/*Number of elements that are allocated from heap at the moment because the pool is in completed use. It should be '0' when pool is not in complete use*/
        int               max_stdalloc;/*Maximum number of allocations from heap after the pool is completely used that are in active use at any point in the life of the process.*/
        char             *name; /*Contains xlator-name:data-type as a string
        struct list_head  global_list;/*This is used to insert it into the global_list of mempools maintained in 'glusterfs-ctx'
};
```

##Life-cycle
```
mem_pool_new (data_type, unsigned long count)

This is a macro which expands to mem_pool_new_fn (sizeof (data_type), count, string-rep-of-data_type)

struct mem_pool *
mem_pool_new_fn (unsigned long sizeof_type, unsigned long count, char *name)

Padded-element:
 ----------------------------------------
|list-ptr|mem-pool-address|in-use|Element|
 ----------------------------------------
 ```

This function allocates the `mem-pool` structure and sets up the pool for use.
`name` parameter above is the `string` containing type of the datatype. This `name` is appended to `xlator-name + ':'` so that it can be easily identified in things like statedump. `count` is the number of elements that need to be allocated. `sizeof_type` is the size of each element. Ideally `('sizeof_type'*'count')` should be the size of the total pool. But to manage the pool using `mem_get`/`mem_put` (will be explained after this section) each element needs to be padded in the front with a `('list', 'mem-pool-address', 'in_use')`. So the actual size of the pool it allocates will be `('padded_sizeof_type'*'count')`. Why these extra elements are needed will be evident after understanding how `mem_get` and `mem_put` are implemented. In this function it just initializes all the `list` structures in front of each element and adds them to the `mem_pool->list` which represent the list of `cold` elements which can be allocated whenever `mem_get` is called on this mem_pool. It remembers mem_pool's start and end addresses in `mem_pool->pool`, `mem_pool->pool_end` respectively. Initializes `mem_pool->cold_count` to `count` and `mem_pool->hot_count` to `0`. This mem-pool will be added to the list of `global_list` maintained in `glusterfs-ctx`


```
void* mem_get (struct mem_pool *mem_pool)

Initial-list before mem-get
----------------
|     Pool       |
|   -----------  |       ----------------------------------------       ----------------------------------------
|  | pool-list | |<---> |list-ptr|mem-pool-address|in-use|Element|<--->|list-ptr|mem-pool-address|in-use|Element|
|   -----------  |       ----------------------------------------       ----------------------------------------
----------------

list after mem-get from the pool
----------------
|     Pool       |
|   -----------  |      ----------------------------------------
|  | pool-list | |<--->|list-ptr|mem-pool-address|in-use|Element|
|   -----------  |      ----------------------------------------
----------------

List when the pool is full:
 ----------------
|     Pool       |       extra element that is allocated
|   -----------  |      ----------------------------------------
|  | pool-list | |     |list-ptr|mem-pool-address|in-use|Element|
|   -----------  |      ----------------------------------------
 ----------------
```

This function is similar to `malloc()` but it gives memory of type `element` of this pool. When this function is called it increments `mem_pool->alloc_count`, checks if there are any free elements in the pool that can be returned by inspecting `mem_pool->cold_count`. If `mem_pool->cold_count` is non-zero then it means there are elements in the pool which are not in active use. It deletes one element from the list of free elements and decrements `mem_pool->cold_count` and increments `mem_pool->hot_count` to indicate there is one more element in active use. Updates `mem_pool->max_alloc` accordingly. Sets `element->in_use` in the padded memory to `1`. Sets `element->mem_pool` address to this mem_pool also in the padded memory(It is useful for mem_put). Returns the address of the memory after the padded boundary to the caller of this function. In the cases where all the elements in the pool are in active use it `callocs` the element with padded size and sets mem_pool address in the padded memory. To indicate the pool-miss and give useful accounting information of the pool-usage it increments `mem_pool->pool_misses`, `mem_pool->curr_stdalloc`. Updates `mem_pool->max_stdalloc` accordingly.

```
void* mem_get0 (struct mem_pool *mem_pool)
```
Just like `calloc` is to `malloc`, `mem_get0` is to `mem_get`. It memsets the memory to all '0' before returning the element.


```
void mem_put (void *ptr)

list before mem-put from the pool
 ----------------
|     Pool       |
|   -----------  |      ----------------------------------------
|  | pool-list | |<--->|list-ptr|mem-pool-address|in-use|Element|
|   -----------  |      ----------------------------------------
 ----------------

list after mem-put to the pool
 ----------------
|     Pool       |
|   -----------  |       ----------------------------------------       ----------------------------------------
|  | pool-list | |<---> |list-ptr|mem-pool-address|in-use|Element|<--->|list-ptr|mem-pool-address|in-use|Element|
|   -----------  |       ----------------------------------------       ----------------------------------------
 ----------------

If mem_put is putting an element not from pool then it is just freed so
no change to the pool
 ----------------
|     Pool       |
|   -----------  |
|  | pool-list | |
|   -----------  |
 ----------------
```

This function is similar to `free()`. Remember that ptr passed to this function is the address of the element, so this function gets the ptr to its head of the padding in front of it. If this memory falls in bettween `mem_pool->pool`, `mem_pool->pool_end` then the memory is part of the 'pool' memory that is allocated so it does some sanity checks to see if the memory is indeed head of the element by checking if `in_use` is set to `1`. It resets `in_use`  to `0`. It gets the mem_pool address stored in the padded region and adds this element to the list of free elements. Decreases `mem_pool->hot_count` increases `mem_pool->cold_count`. In the case where padded-element address does not fall in the range of `mem_pool->pool`, `mem_pool->pool_end` it just frees the element and decreases `mem_pool->curr_stdalloc`.

```
void
mem_pool_destroy (struct mem_pool *pool)
```
Deletes this pool from the `global_list` maintained by `glusterfs-ctx` and frees all the memory allocated in `mem_pool_new`.


###How to pick pool-size
This varies from work-load to work-load. Create the mem-pool with some random size and run the work-load. Take the statedump after the work-load is complete. In the statedump if `max_alloc` is always less than `cold_count` may be reduce the size of the pool closer to `max_alloc`. On the otherhand if there are lots of `pool-misses` then increase the `pool_size` by `max_stdalloc` to achieve better 'hit-rate' of the pool.
