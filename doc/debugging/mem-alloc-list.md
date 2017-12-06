## Viewing Memory Allocations

While statedumps provide stats of the number of allocations, size etc for a
particular mem type, there is no easy way to examine all the allocated objects of that type
in memory.Being able to view this information could help with determining how an object is used,
and if there are any memory leaks.

The mem_acct_rec structures have been updated to include lists to which the allocated object is
added. These can be examined in gdb using simple scripts.

`gdb> plist xl->mem_acct.rec[$type]->obj_list`

will print out the pointers of all allocations of $type.

These changes are primarily targeted at developers and need to enabled
at compile-time using `configure --enable-debug`.



