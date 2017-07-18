Thread Naming
================
Gluster processes spawn many threads; some threads are created by libglusterfs
library, while others are created by xlators. When gfapi library is used in an
application, some threads belong to the application and some are spawned by
gluster libraries. We also have features where n number of threads are spawned
to act as worker threads for same operation.

In all the above cases, it is useful to be able to determine the list of threads
that exist in runtime. Naming threads when you create them is the easiest way to
provide that information to kernel so that it can then be queried by any means.

How to name threads
-------------------
We have two wrapper functions in libglusterfs for creating threads. They take
name as an argument and set thread name after its creation.

```C
gf_thread_create (pthread_t *thread, const pthread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg, const char *name)
```

```C
gf_thread_create_detached (pthread_t *thread,
                           void *(*start_routine)(void *), void *arg,
                           const char *name)
```

As max name length for a thread in POSIX is only 16 characters including the
'\0' character, you have to be a little creative with naming. Also, it is
important that all Gluster threads have common prefix. Considering these
conditions, we have "gluster" as prefix for all the threads created by these
wrapper functions. It is responsibility of the owner of thread to provide the
suffix part of the name. It does not have to be a descriptive name, as it has
only 8 letters to work with. However, it should be unique enough such that it
can be matched with a table which describes it.

If n number of threads are spwaned to perform same function, it is must that the
threads are numbered.

Table of thread names
---------------------
Thread names don't have to be a descriptive; however, it should be unique enough
such that it can be matched with a table below without ambiguity.

- bdaio    - block device aio
- brfsscan - bit rot fs scanner
- brhevent - bit rot event handler
- brmon    - bit rot monitor
- brosign  - bit rot one shot signer
- brpobj   - bit rot object processor
- brsproc  - bit rot scrubber
- brssign  - bit rot stub signer
- brswrker - bit rot worker
- clogc    - changelog consumer
- clogcbki - changelog callback invoker
- clogd    - changelog dispatcher
- clogecon - changelog reverse connection
- clogfsyn - changelog fsync
- cloghcon - changelog history consumer
- clogjan  - changelog janitor
- clogpoll - changelog poller
- clogproc - changelog process
- clogro   - changelog rollover
- ctrcomp  - change time recorder compaction
- dhtdf    - dht defrag task
- dhtdg    - dht defrag start
- dhtfcnt  - dht rebalance file counter
- ecshd    - ec heal daemon
- epollN   -  epoll thread
- fdlwrker - fdl worker
- fusenoti - fuse notify
- fuseproc - fuse main thread
- gdhooks  - glusterd hooks
- glfspoll -  gfapi poller thread
- idxwrker - index worker
- iosdump  - io stats dump
- iotwr    - io thread worker
- jbrflush - jbr flush
- leasercl - lease recall
- memsweep - sweeper thread for mem pools
- nfsauth  - nfs auth
- nfsnsm   - nfs nsm
- nfsudp   - nfs udp mount
- nlmmon   - nfs nlm/nsm mon
- posixaio - posix aio
- posixfsy - posix fsync
- posixhc  - posix heal
- posixjan - posix janitor
- quiesce  - quiesce dequeue
- rdmaAsyn - rdma async event handler
- rdmaehan - rdma completion handler
- rdmarcom - rdma receive completion handler
- rdmascom - rdma send completion handler
- rpcsvcrh - rpcsvc request handler
- scleanup - socket cleanup
- shdheal  - self heal daemon
- sigwait  -  glusterfsd sigwaiter
- spoller  - socket poller
- sprocN   - syncop worker thread
- tbfclock - token bucket filter token generator thread
- tierfixl - tier fix layout
- timer    - timer thread
- upreaper - upcall reaper
