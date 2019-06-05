# Resource usage reduction in brick multiplexing

Each brick is regresented with a graph of translators in a brick process.
Each translator in the graph has its own set of threads and mem pools
and other system resources allocations. Most of the times all these
resources are not put to full use. Reducing the resource consumption
of each brick is a problem in itself that needs to be addressed. The other
aspect to it is, sharing of resources across brick graph, this becomes
critical in brick multiplexing scenario. In this document we will be discussing
only about the threads.

If a brick mux process hosts 50 bricks there are atleast 600+ threads created
in that process. Some of these are global threads that are shared by all the
brick graphs, and others are per translator threads. The global threads like
synctask threads, timer threads, sigwaiter, poller etc. are configurable and
do not needs to be reduced. The per translator threads keeps growing as the
number of bricks in the process increases. Each brick spawns atleast 10+
threads:
- io-threads
- posix threads:
     1. Janitor
     2. Fsyncer
     3. Helper
     4. aio-thread
- changelog and bitrot threads(even when the features are not enabled)

## io-threads

io-threads should be made global to the process, having 16+ threads for
each brick does not make sense. But io-thread translator is loaded in
the graph, and the position of io-thread translator decides from when
the fops will be parallelised across threads. We cannot entirely move
the io-threads to libglusterfs and say the multiplexing happens from
the master translator or so. Hence, the io-thread orchestrator code
is moved to libglusterfs, which ensures there is only one set of
io-threads that is shared among the io-threads translator in each brick.
This poses performance issues due to lock-contention in the io-threds
layer. This also shall be addressed by having multiple locks instead of
one global lock for io-threads.

## Posix threads
Most of the posix threads execute tasks in a timely manner, hence it can be
replaced with a timer whose handler register a task to synctask framework, once
the task is complete, the timer is registered again. With this we can eliminate
the need of one thread for each task. The problem with using synctasks is
the performance impact it will have due to make/swapcontext. For task that
does not involve network wait, we need not do makecontext, instead the task
function with arg can be stored and executed when a synctask thread is free.
We need to implement an api in synctask to execute atomic tasks(no network wait)
without the overhead of make/swapcontext. This will solve the performance
impact associated with using synctask framework.

And the other challenge, is to cancel all the tasks pending from a translator.
This is important to cleanly detach brick. For this, we need to implement an
api in synctask that can cancel all the tasks from a given translator.

For future, this will be replced to use global thread-pool(once implemented).

## Changelog and bitrot threads

In the initial implementation, the threads are not created if the feature is
not enabled. We need to share threads across changelog instances if we plan
to enable these features in brick mux scenario.

