Generic buffer infrastructure
=============================

Generic buffer is the simple concept to implement basic operations on
the memory area of the known size. Basically the buffer is just a pair
of raw C pointer and size; if the last byte of the pointed area is '\0',
this area can be treated as a C string.

Generic buffers are intended to be a function- or block-scoped. Going
down to the storage management, the basic idea is to maintain an
internal memory allocated with `alloca()` for small areas or with
`malloc()` for the larger ones. For the latter, an allocated buffer
should be freed with the function specified by using GCC (and now
Clang as well) specific `__attribute__((__cleanup__(...)))` variable
declaration.

API is very simple and mostly covers initialization, copying, clearing,
concatenation and basic formatted output. Macros with name ending in
`_string()` operates on strings (i. e. areas where the last byte is
set to '\0'), and `_data()` counterparts assumes raw memory areas.

### Internal area allocation

All allocation sizes are rounded up to the next greater multiple
of `GF_BUF_ROUNDUP`. If the result exceeds `GF_BUF_STACK_THRESHOLD`,
allocation request is served with `GF_MALLOC()`, and allocated area
will be freed by `gf_buf_cleanup()` when the variable goes out of
scope. Otherwise it's served with `alloca()`, so freeing is performed
automatically when the call stack is destroyed.

### Declaration

`gf_buf_decl_null(&buf)` declares null buffer. This buffer should be
initialized before doing anything useful.

`gf_buf_decl_string(&buf)` declares the buffer which is equivalent to
empty (zero-length) string "". Nothing is allocated, neither on stack
or on heap (i. e. buffer internally references a constant C string).

`gf_buf_decl_init_string(&buf, "text")` declares the buffer which is
equivalent to string "text". Again, the buffer internally references
a constant C string. Note that `gf_buffer_decl_string(&buf)` is just
a useful shortcut for `gf_buffer_decl_init_string(&buf, "")`.

`gf_buf_decl_init_string(&buf, ptr)` with non-constant `ptr` declares
the buffer which internally references a C string pointed by `ptr`,
i. e. nothing is allocated or copied. So if the data pointed by `ptr`
is changed during the lifetime of `buf`, the behavior is undefined. If
`ptr` is NULL or not initialized, the behavior is undefined as well.

`gf_buf_decl_init_data(&buf, data, size)` is very similar to the above
but declares the buffer holding the reference to `size`-bytes memory
area pointed by `data`.

### Basic operations

`gf_buf_reset(&buf)` unconditionally (regardless of an actual type and
contents) resets an internal state of `buf` to null buffer, i. e. as
if it is just declared with `gf_buf_decl_null(&buf)`.

`gf_buf_clear_string(&buf)` clears the buffer by setting its data to
empty (zero-length) string "". In contrast with `gf_buf_decl_string()`,
if `buf` references a constant C string, an internal area for the
zero-length string is dynamically allocated. An underlying buffer
type is forced to be a string.

`gf_buf_clear_data(&buf)` is very similar to the above but sets the
buffer's data size to 0, so the buffer should be considered as empty.
An underlying buffer type is forced to be a raw buffer.

`gf_buf_assign_string(&buf, "text")` or `gf_buf_assign(&buf, ptr)`
copies the provided C string into a buffer, allocating or enlarging
an internal area if necessary. Underlying buffer type is forced to
be a string.

`gf_buf_assign_data(&buf, data, size)` is very similar to the above but
copies the provided memory area of specified. size. Underlying buffer
type is forced to be a raw buffer.

`gf_buf_copy(&to, &from)` copies one buffer to another, allocating or
enlarging destination's buffer internal area if necessary. Underlying
buffer type is copied as well.

`gf_buf_equal(&buf0, &buf1)` compares two buffers as memory areas.
Equal buffers have byte-to-byte contents of the same length. Note
that the size of an internally allocated area may be different for
equal buffers.

`gf_buf_data(&buf)` returns raw pointer to an internally allocated
area. This pointer should be considered as read-only and not intented
to alter the buffer contents.

`gf_buf_data_size(&buf)` returns size of data currently stored in the
buffer. If an underlying buffer type is a string, terminating null byte
is included.

`gf_buf_copyout(&buf)` copies the buffer contents with `gf_memdup()`
and returns the pointer to newly allocated area. For the null buffer,
NULL pointer is returned.

`gf_buf_compact(&buf)` shrinks the buffer so internally allocated area
becomes of exactly the same size as actually used. Note that the
buffers are never compacted implicitly.

### Concatenation and formatted output

`gf_buf_concat(&dst, &src)` concatenates the contents of two buffers
into the first one, enlarging its internal area if necessary. Both
buffers may be strings or raw buffers, and concatenation result is
defined as follows:

  - If both `dst` and `src` are strings, null byte at the end of `dst`
    is overwritten with the first byte from `src`, and the result is
    forced to be a string. This is a (hopefully) safe analog of
    `strcat()` from standard C library.

  - If both `dst` and `src` are raw buffers, the memory areas are
    adjacently concatenated, and the result is forced to be a raw
    buffer.

  - If `dst` is a string but `src` is not, null byte at the end of
    `dst` is overwritten by the first byte of adjacently concatenated
    area from `src`, and the result is forced to be a raw buffer.

  - If `dst` is a raw buffer but `src` is a string, data from `src` is
    adjacently concatenated with `dst` including '\0' byte, and the
    result is forced to be a string.

`gf_buf_concat_string(&buf, str)` is very similar to the above but
concatenates the contents of the buffer with C string (so the result
is always forced to be a string as well).

`gf_buf_sprintf(&buf, fmt, args...)` ensures that an internally
allocated area is large enough to hold the result of
`sprintf(fmt, args...)` and performs the formatted output into
the buffer.

`gf_buf_snprintf(&buf, size, fmt, args...)` is similar but writes
at most `size` bytes, including terminating '\0', into the buffer.
