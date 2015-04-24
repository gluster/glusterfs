#syncop framework
A coroutines-based, cooperative multi-tasking framework.

## Topics

- Glossary
- Lifecycle of a synctask
- Existing usage


## Glossary

### syncenv

syncenv is an object that provides access to a pool of worker threads.
synctasks execute in a syncenv.

### synctask

synctask can be informally defined as a pair of function pointers, namely _the
call_ and _the callback_ (see syncop.h for more details).

        synctask_fn_t - 'the call'
        synctask_cbk_t - 'the callback'

synctask has two modes of operation,

1. The calling thread waits for the synctask to complete.
2. The calling thread schedules the synctask and continues.

synctask guarantees that the callback is called _after_ the call completes.

### Lifecycle of a synctask

A synctask could go into the following stages while in execution.

- CREATED  - On calling synctask_create/synctask_new.

- RUNNABLE - synctask is queued in env->runq.

- RUNNING  - When one of syncenv's worker threads calls synctask_switch_to.

- WAITING  - When a synctask calls synctask_yield.

- DONE     - When a synctask has run to completion.


                                +-------------------------------+
                                |            CREATED            |
                                +-------------------------------+
                                  |
                                  | synctask_new/synctask_create
                                  v
                                +-------------------------------+
                                |    RUNNABLE (in env->runq)    | <+
                                +-------------------------------+  |
                                  |                                |
                                  | synctask_switch_to             |
                                  v                                |
 +------+  on task completion   +-------------------------------+  |
 | DONE | <-------------------- |            RUNNING            |  | synctask_wake/wake
 +------+                       +-------------------------------+  |
                                  |                                |
                                  | synctask_yield/yield           |
                                  v                                |
                                +-------------------------------+  |
                                |    WAITING (in env->waitq)    | -+
                                +-------------------------------+

Note: A synctask is not guaranteed to run on the same thread throughout its
lifetime. Every time a synctask yields, it is possible for it to run on a
different thread.
