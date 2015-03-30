performance/write-behind translator
===================================

Basic working
--------------

Write behind is basically a translator to lie to the application that the 
write-requests are finished, even before it is actually finished.

On a regular translator tree without write-behind, control flow is like this:

1. application makes a `write()` system call.
2. VFS ==> FUSE ==> `/dev/fuse`.
3. fuse-bridge initiates a glusterfs `writev()` call.
4. `writev()` is `STACK_WIND()`ed up to client-protocol or storage translator.
5. client-protocol, on receiving reply from server, starts `STACK_UNWIND()` towards the fuse-bridge.

On a translator tree with write-behind, control flow is like this:

1. application makes a `write()` system call.
2. VFS ==> FUSE ==> `/dev/fuse`.
3. fuse-bridge initiates a glusterfs `writev()` call.
4. `writev()` is `STACK_WIND()`ed up to write-behind translator.
5. write-behind adds the write buffer to its internal queue and does a `STACK_UNWIND()` towards the fuse-bridge.

write call is completed in application's percepective. after 
`STACK_UNWIND()`ing towards the fuse-bridge, write-behind initiates a fresh 
writev() call to its child translator, whose replies will be consumed by 
write-behind itself. Write-behind _doesn't_ cache the write buffer, unless 
`option flush-behind on` is specified in volume specification file.

Windowing
---------

With respect to write-behind, each write-buffer has three flags: `stack_wound`, `write_behind` and `got_reply`.

* `stack_wound`: if set, indicates that write-behind has initiated `STACK_WIND()` towards child translator.
* `write_behind`: if set, indicates that write-behind has done `STACK_UNWIND()` towards fuse-bridge.
* `got_reply`: if set, indicates that write-behind has received reply from child translator for a `writev()` `STACK_WIND()`. a request will be destroyed by write-behind only if this flag is set.

Currently pending write requests = aggregate size of requests with write_behind = 1 and got_reply = 0.

window size limits the aggregate size of currently pending write requests. once 
the pending requests' size has reached the window size, write-behind blocks  
writev() calls from fuse-bridge. Blocking is only from application's 
perspective. Write-behind does `STACK_WIND()` to child translator 
straight-away, but hold behind the `STACK_UNWIND()` towards fuse-bridge. 
`STACK_UNWIND()` is done only once write-behind gets enough replies to 
accommodate for currently blocked request.

Flush behind
------------

If `option flush-behind on` is specified in volume specification file, then 
write-behind sends aggregate write requests to child translator, instead of 
regular per request `STACK_WIND()`s.
