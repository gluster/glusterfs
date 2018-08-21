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
    INTERRUPT_NONE,
    INTERRUPT_SQUELCHED,
    INTERRUPT_HANDLED,
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
The flush interrupt handling code is guarded by the
`flush_handle_interrupt` Boolean member of `fuse_private_t`.

### Usage

A given FUSE fop can be enabled to handle interrupts via the following
steps:

- Define a handler function (of type `fuse_interrupt_handler_t`).
  It should implement the interrupt handling logic and in the end
  call (directly or as async callback) `fuse_interrupt_finish_interrupt()`.
  The `intstat` argument to `fuse_interrupt_finish_interrupt` should be
  either `INTERRUPT_SQUELCHED` or `INTERRUPT_HANDLED`.
    - `INTERRUPT_SQUELCHED` means that we choose not to handle the interrupt
      and the fop is going on uninterrupted.
    - `INTERRUPT_HANDLED` means that the interrupt was actually handled. In
      this case the fop will be answered from interrupt context with errno
      `EINTR` (that is, the fop should not send a response to the kernel).

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
      interrupt was handled. If it was, then the fuse request is already
      answered and the stack gets destroyed in `fuse_interrupt_finish_fop` so
      `fuse_<FOP>_cbk` can just return (zero). Otherwise follow the standard
      cbk logic (answer the fuse request and destroy the stack -- these are
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
          fop handler will have `datap` set.
        - If `sync` is false, and handlers pass a pointer to a pointer for
          `datap`, they should check if the pointed pointer is NULL before
          attempting to deal with the data.
