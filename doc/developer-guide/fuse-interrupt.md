# Fuse interrupt handling

## Conventions followed

- *FUSE* refers to the "wire protocol" between kernel and userspace and
  related specifications.
- *fuse* refers to the kernel subsystem and also to the GlusterFs translator.

## FUSE interrupt handling spec

The [Linux kernel FUSE documentation](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/filesystems/fuse.txt?h=v4.18#n148)
desrcibes how interrupt handling happens in fuse.

## Interrupt handling in the fuse translator

### Declarations

This document describes the internal API in the fuse translator with which
interrupt can be handled.

The API being internal (to be used only in fuse-bridge.c; the functions are
not exported to a header file).

```
enum fuse_interrupt_state {
    /* ... */
    INTERRUPT_SQUELCHED,
    INTERRUPT_HANDLED,
    /* ... */
};
typedef enum fuse_interrupt_state fuse_interrupt_state_t;
struct fuse_interrupt_record;
typedef struct fuse_interrupt_record fuse_interrupt_record_t;
typedef void (*fuse_interrupt_handler_t)(xlator_t *this,
                                         fuse_interrupt_record_t *);
struct fuse_interrupt_record {
    fuse_in_header_t fuse_in_header;
    void *data;
    /*
       ...
     */
};

fuse_interrupt_record_t *
fuse_interrupt_record_new(fuse_in_header_t *finh,
                          fuse_interrupt_handler_t handler);

void
fuse_interrupt_record_insert(xlator_t *this, fuse_interrupt_record_t *fir);

gf_boolean_t
fuse_interrupt_finish_fop(call_frame_t *frame, xlator_t *this,
                          gf_boolean_t sync, void **datap);

void
fuse_interrupt_finish_interrupt(xlator_t *this, fuse_interrupt_record_t *fir,
                                fuse_interrupt_state_t intstat,
                                gf_boolean_t sync, void **datap);
```

The code demonstrates the usage of the API through `fuse_flush()`. (It's a
dummy implementation only for demonstration purposes.) Flush is chosen
because a `FLUSH` interrupt is easy to trigger (see
*tests/features/interrupt.t*). Interrupt handling for flush is switched on
by `--fuse-flush-handle-interrupt` (a hidden glusterfs command line flag).
The implementation of flush interrupt is contained in the
`fuse_flush_interrupt_handler()` function and blocks guarded by the

```
if (priv->flush_handle_interrupt) { ...
```

conditional (where `priv` is a `*fuse_private_t`).

### Overview

"Regular" fuse fops and interrupt handlers interact via a list containing
interrupt records.

If a fop wishes to have its interrupts handled, it needs to set up an
interrupt record and insert it into the list; also when it's to finish
(ie. in its "cbk" stage) it needs to delete the record from the list.

If no interrupt happens, basically that's all to it - a list insertion
and deletion.

However, if an interrupt comes for the fop, the interrupt FUSE request
will carry the data identifying an ongoing fop (that is, its `unique`),
and based on that, the interrupt record will be looked up in the list, and
the specific interrupt handler (a member of the interrupt record) will be
called.

Usually the fop needs to share some data with the interrupt handler to
enable it to perform its task (also shared via the interrupt record).
The interrupt API offers two approaches to manage shared data:
- _Async or reference-counting strategy_: from the point on when the interrupt
  record is inserted to the list, it's owned jointly by the regular fop and
  the prospective interrupt handler. Both of them need to check before they
  return if the other is still holding a reference; if not, then they are
  responsible for reclaiming the shared data.
- _Sync or borrow strategy_: the interrupt handler is considered a borrower
  of the shared data. The interrupt handler should not reclaim the shared
  data. The fop will wait for the interrupt handler to finish (ie., the borrow
  to be returned), then it has to reclaim the shared data.

The user of the interrupt API need to call the following functions to
instrument this control flow:
- `fuse_interrupt_record_insert()` in the fop to insert the interrupt record to
   the list;
- `fuse_interrupt_finish_fop()`in the fop (cbk) and
- `fuse_interrupt_finish_interrupt()`in the interrupt handler

to perform needed synchronization at the end their tenure. The data management
strategies are implemented by the `fuse_interrupt_finish_*()` functions (which
have an argument to specify which strategy to use); these routines take care
of freeing the interrupt record itself, while the reclamation of the shared data
is left to the API user.

### Usage

A given FUSE fop can be enabled to handle interrupts via the following
steps:

- Define a handler function (of type `fuse_interrupt_handler_t`).
  It should implement the interrupt handling logic and in the end
  call (directly or as async callback) `fuse_interrupt_finish_interrupt()`.
  The `intstat` argument to `fuse_interrupt_finish_interrupt` should be
  either `INTERRUPT_SQUELCHED` or `INTERRUPT_HANDLED`.
    - `INTERRUPT_SQUELCHED` means that the interrupt could not be delivered
      and the fop is going on uninterrupted.
    - `INTERRUPT_HANDLED` means that the interrupt was actually handled. In
      this case the fop will be answered from interrupt context with errno
      `EINTR` (that is, the fop should not send a response to the kernel).

  (the enum `fuse_interrupt_state` includes further members, which are reserved
  for internal use).

  We return to the `sync` and `datap` arguments later.
- In the `fuse_<FOP>` function create an interrupt record using
  `fuse_interrupt_record_new()`, passing the incoming `fuse_in_header` and
  the above handler function to it.
    - Arbitrary further data can be referred to via the `data` member of the
      interrupt record that is to be passed on from fop context to
      interrupt context.
- When it's set up, pass the interrupt record to
  `fuse_interrupt_record_insert()`.
- In `fuse_<FOP>_cbk` call `fuse_interrupt_finish_fop()`.
    - `fuse_interrupt_finish_fop()` returns a Boolean according to whether the
      interrupt was handled. If it was, then the FUSE request is already
      answered and the stack gets destroyed in `fuse_interrupt_finish_fop` so
      `fuse_<FOP>_cbk()` can just return (zero). Otherwise follow the standard
      cbk logic (answer the FUSE request and destroy the stack -- these are
      typically accomplished by `fuse_err_cbk()`).
- The last two argument of `fuse_interrupt_finish_fop()` and
  `fuse_interrupt_finish_interrupt()` are `gf_boolean_t sync` and
  `void **datap`.
    - `sync` represents the strategy for freeing the interrupt record. The
      interrupt handler and the fop handler are in race to get at the interrupt
      record first (interrupt handler for purposes of doing the interrupt
      handling, fop handler for purposes of deactivating the interrupt record
      upon completion of the fop handling).
        - If `sync` is true, then the fop handler will wait for the interrupt
          handler to finish and it takes care of freeing.
        - If `sync` is false, the loser of the above race will perform freeing.

      Freeing is done within the respective interrupt finish routines, except
      for the `data` field of the interrupt record; with respect to that, see
      the discussion of the `datap` parameter below. The strategy has to be
      consensual, that is, `fuse_interrupt_finish_fop()` and
      `fuse_interrupt_finish_interrupt()` must pass the same value for `sync`.
      If dismantling the resources associated with the interrupt record is
      simple, `sync = _gf_false` is the suggested choice; `sync = _gf_true` can
      be useful in the opposite case, when dismantling those resources would
      be inconvenient to implement in two places or to enact in non-fop context.
    - If `datap` is `NULL`, the `data` member of the interrupt record will be
      freed within the interrupt finish routine.  If it points to a valid
      `void *` pointer, and if caller is doing the cleanup (see `sync` above),
      then that pointer will be directed to the `data` member of the interrupt
      record and it's up to the caller what it's doing with it.
        - If `sync` is true, interrupt handler can use `datap = NULL`, and
          fop handler will have `datap` point to a valid pointer.
        - If `sync` is false, and handlers pass a pointer to a pointer for
          `datap`, they should check if the pointed pointer is NULL before
          attempting to deal with the data.

### FUSE answer for the interrupted fop

The kernel acknowledges a successful interruption for a given FUSE request
if the filesystem daemon answers it with errno EINTR; upon that, the syscall
which induced the request will be abruptly terminated with an interrupt, rather
than returning a value.

In glusterfs, this can be arranged in two ways.

- If the interrupt handler wins the race for the interrupt record, ie.
  `fuse_interrupt_finish_fop()` returns true to `fuse_<FOP>_cbk()`, then, as
  said above, `fuse_<FOP>_cbk()` does not need to answer the FUSE request.
  That's because then the interrupt handler will take care about answering
  it (with errno EINTR).
- If `fuse_interrupt_finish_fop()` returns false to `fuse_<FOP>_cbk()`, then
  this return value does not inform the fop handler whether there was an interrupt
  or not. This return value occurs both when fop handler won the race for the
  interrupt record against the interrupt handler, and when there was no interrupt
  at all.

  However, the internal logic of the fop handler might detect from other
  circumstances that an interrupt was delivered. For example, the fop handler
  might be sleeping, waiting for some data to arrive, so that a premature
  wakeup (with no data present) occurs if the interrupt handler intervenes. In
  such cases it's the responsibility of the fop handler to reply the FUSE
  request with errro EINTR.
