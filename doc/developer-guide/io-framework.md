# I/O Framework for GlusterFS

This framework provides a basic infrastructure to abstract I/O operations
from the actual system calls to support multiple lower level I/O
implementations.

## Introduction

This abstraction makes it possible to always use the same generic API for
I/O related operations independently of how they are internally implemented.

The changes required to use this framework can be significant given that
it's based on a callback architecture while current implementation is
basically sequential. For this reason it will be very useful that the
framework can fully replace the current code to avoid maintaining two
very different implementations, even if the legacy implementation is used.

For example, in the current implementation there are two supported ways to
do I/O:

- **Synchronous I/O** _(legacy implementation)_
  This is the most simple approach. Every time a I/O operation is done,
  it's executed in the foreground, blocking the executing thread until
  the operation finishes.

- **io_uring I/O** ^[1]
  This is a new and powerful kernel API that provides asynchronous I/O
  execution with little overhead. This approach is superior because it
  doesn't block the executing thread, allowing more work to be done while
  the I/O operation is being processed in the background.

- **threaded I/O**
  This mode is not yet implemented, but it should be a replacement for the
  legacy mode when io_uring is not present. It will have all the advantages
  related to the thread pool, but it will use another set of system calls
  for actual I/O operations instead of the io_uring system calls. Worker
  threads won't be blocked during I/O.

_io_uring_ is only present on latest linux kernels and it's dynamically
detected and used if available. Otherwise it silently fails back to the
synchronous implementation in a transparent way for the rest of the code
that uses this framework (once implemented, it will use the threaded I/O
instead of the legacy mode when io_uring is not supported).

The implementation is done with the io_uring API in mind. This means that
io_uring fits very well in the I/O framework API, and the other modes
are adjusted to follow the same semantics.

## How to use it

In this section a general overview of the operation will be provided,
focused on the io_uring-based implementation. For differences when
io_uring is not present, check section [Fallback mode](#fallback-mode)

### Initialization

The framework is initialized using `gf_io_run()`.

```c
typedef int32_t (*gf_io_async_t)(gf_io_op_t *op);

typedef struct {
    gf_io_async_t setup;
    gf_io_async_t cleanup;
} gf_io_handlers_t;

int32_t gf_io_run(gf_io_handlers_t handlers, void *data);
```

The handlers structure contains two async functions, one that is called
just after having initialized the I/O infrastructure, and another one that
is called after stopping everything else. The 'data' argument is an extra
argument that will be passed to each function.

The returned value can be a negative error code if there has been any
problem while initializing the system, or 0 if everything worked fine. In
this case, the function only returns when the program is terminating.

### Termination

When it's determined that the process must be terminated, a call to
`gf_io_shutdown()` must be done.

```c
void gf_io_shutdown(void);
```

This function initiates a shutdown procedure, but returns immediately.
Once the shutdown is completed, `gf_io_run()` will return. It can be
called from anywhere.

When shutdown is initiated, all I/O should have been stopped. If there
is active I/O during the shutdown, they can complete, fail or be cancelled,
depending on what state the request was. To ensure consistent behavior, try
to always stop I/O before terminating the I/O framework.

> **Note**: This function is not yet implemented because even with the io_uring
> engine we still rely on gf_event_dispatch() function to run the main program
> loop. Once the events infrastructure is integrated into the I/O framework,
> this function will be available.

### Normal operation

After everything is ready, the normal operation of the I/O framework is
very simple:

1. A worker picks one completion event from the kernel.

2. The callback associated to the completion event is executed.

   2.1. The callback can prepare new I/O requests using one of the
        `gf_io_*` I/O functions available for I/O operations.

   2.2. Requests can be sent one by one or submitted in a batch. In all
        cases they are added to the io_uring SQ ring.

3. Once the callback finishes, any queued requests (from this worker or
   any other worker that has added requests to the queue) are automatically
   flushed.

### Available I/O operations

The I/O framework supports two ways of sending operations to the kernel.
In direct mode, each request is sent independently of the others. In batch
mode multiple requests are sent together all at once.

All operations will also have a `data` argument to pass any additional
per-request private data that the callback may need. This data will be
available in `op->data` for most of the cases (there's an exception for
asynchronous requests. See later).

Many of the I/O operations will have a timeout argument, which represents
the maximum time allowed for the I/O to complete. If the operation takes
more than that time, the system call will be cancelled and the callback
will be executed passing a `-ETIMEDOUT` error.

I/O operations will also have a priority argument that makes it possible
to give different priorities to each requests so that the kernel scheduler
can efficiently manage them based on their priority.

An identifier is returned for each request. This value can be used to try
to cancel the associated request if it has not been started or completed
yet.

#### Direct mode

In direct mode the interface is really simple. Each function only requires
the data needed to perform the operation and returns an identifier. No
memory allocations are needed.

#### Batch mode

In batch mode a `gf_io_batch_t` object needs to be created, which will
contain all requests to send. Then one or more `gf_io_request_t` objects
need to be created and added to the batch object. Both types of objects
can be allocated in the stack because they are not needed once the batch
is submitted.

The functions to prepare requests have the same name as those used in
direct mode but with the `_prepare` suffix. The function signature is
exactly the same, but adding a `gf_io_request_t` argument.

Once a request is prepared, it can be added to the batch object using
`gf_io_batch_add()`. This function also receives a pointer to an identifier.
If it's not NULL, the id of this request will be copied to that location
once the batch is submitted.

Optionally, it's possible to create a chain of dependencies between requests
of a batch. In this case, a chained request will only be executed once the
previous request has finished with a success.

Once the batch is ready, it can be processed by calling `gf_io_batch_submit()`.

##### Operations

###### Cancel request

```c
uint64_t
gf_io_cancel(gf_io_callback_t cbk, uint64_t ref, void *data);
```

Tries to cancel the request identified by `ref`. `cbk` will be called with
error 0 if the request has been cancelled, `-ENOENT` if the request cannot
be found (it has already terminated probably), or `-EALREADY` if the request
is still there but cannot be cancelled.

In case that the request can be successfully cancelled, the callback
associated to that request will be called with error `-ECANCELED`.

###### Callback request

```c
uint64_t
gf_io_callback(gf_io_callback_t cbk, void *data);
```

This request simply causes the `cbk` to be executed in the background.
Error is always 0 and can be ignored.

###### Asynchronous request

```c
uint64_t
gf_io_async(gf_io_async_t async, void *data, gf_io_callback_t cbk,
            void *cbk_data);
```

This is very similar to a callback request, but it provides the `async`
function that does something that can potentially fail (i.e. return an
error), and a `cbk` that will be called once the previous function completes.
The callback will receive the error code returned by the asynchronous
function.

#### Read request

```c
uint64_t
gf_io_preadv(gf_io_callback_t cbk, void *data, int32_t fd,
             const struct iovec *iov, uint32_t count, uint64_t offset,
             int32_t flags, uint64_t to, int32_t prio);
```

> **Note**: Example I/O request. Not yet implemented.

#### Write request

```c
uint64_t
gf_io_writev(gf_io_callback_t cbk, void *data, int32_t fd,
             const struct iovec *iov, uint32_t count, uint64_t offset,
             int32_t flags, uint64_t to, int32_t prio);
```

> **Note**: Example I/O request. Not yet implemented.

## API Reference

### Types

**gf_io_worker_t**: Context information of a worker.

**gf_io_request_t**: Object to track requests.

**gf_io_callback_t**: Callback function signature to process completion events.

**gf_io_mode_t**: Enumeration of available I/O modes.

### Functions

**gf_io_run**: Main initialization function.

```c
int32_t
gf_io_run();
```

**gf_io_shutdown**: Trigger termination of the I/O framework.

```c
void
gf_io_shutdown();
```

**gf_io_mode**: Check the current running mode.

```c
gf_io_mode_t
gf_io_mode();
```

## Fallback mode

When _io_uring_ cannot be started for any reason, the framework falls back
to a legacy operation mode. In this mode the API will be the same but it
will work in a more simpler way. In this case, the thread pool won't be
started.

The most important difference is that most of the requests are processed
as soon as they are initialized, for example in `gf_io_readv()` a `readv()`
system call will be executed synchronously. The result will be kept into
the request object.

When a request is added to a worker with `gf_io_worker_add()`, instead of
deferring the execution of the callback till the worker processes it, the
callback will be immediately executed.

The other functions do nothing in this mode.

## Remaining improvements

- Reorganize initialization and termination of the process
- Replace io-threads
- Move fuse I/O to this framework
- Move posix I/O to this framework
- Move sockets I/O to this framework
- Move timers to this framework
- Move synctasks to this framework
- Implement a third threaded mode not based on io_uring

[1]: https://kernel.dk/io_uring.pdf
