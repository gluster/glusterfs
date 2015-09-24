Translator development
======================

Setting the Stage
-----------------

This is the first post in a series that will explain some of the details of
writing a GlusterFS translator, using some actual code to illustrate.

Before we begin, a word about environments. GlusterFS is over 300K lines of
code spread across a few hundred files. That's no Linux kernel or anything, but
 you're still going to be navigating through a lot of code in every
code-editing session, so some kind of cross-referencing is *essential*. I use
cscope with the vim bindings, and if I couldn't do Crtl+G and such to jump
between definitions all the time my productivity would be cut in half. You may
prefer different tools, but as I go through these examples you'll need
something functionally similar to follow on. OK, on with the show.

The first thing you need to know is that translators are not just bags of
functions and variables. They need to have a very definite internal structure
so that the translator-loading code can figure out where all the pieces are.
The way it does this is to use dlsym to look for specific names within your
shared-object file, as follow (from `xlator.c`):

```
if (!(xl->fops = dlsym (handle, "fops"))) {
    gf_log ("xlator", GF_LOG_WARNING, "dlsym(fops) on %s",
        dlerror ());
    goto out;
}

if (!(xl->cbks = dlsym (handle, "cbks"))) {
    gf_log ("xlator", GF_LOG_WARNING, "dlsym(cbks) on %s",
        dlerror ());
    goto out;
}

if (!(xl->init = dlsym (handle, "init"))) {
    gf_log ("xlator", GF_LOG_WARNING, "dlsym(init) on %s",
        dlerror ());
    goto out;
}

if (!(xl->fini = dlsym (handle, "fini"))) {
    gf_log ("xlator", GF_LOG_WARNING, "dlsym(fini) on %s",
        dlerror ());
    goto out;
}
```

In this example, `xl` is a pointer to the in-memory object for the translator
we're loading. As you can see, it's looking up various symbols *by name* in the
 shared object it just loaded, and storing pointers to those symbols. Some of
them (e.g. init) are functions, while others (e.g. fops) are dispatch tables
containing pointers to many functions. Together, these make up the translator's
 public interface.

Most of this glue or boilerplate can easily be found at the bottom of one of
the source files that make up each translator. We're going to use the `rot-13`
translator just for fun, so in this case you'd look in `rot-13.c` to see this:

```
struct xlator_fops fops = {
    .readv          = rot13_readv,
    .writev         = rot13_writev
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
{ .key    = {"encrypt-write"},
  .type = GF_OPTION_TYPE_BOOL
},
{ .key    = {"decrypt-read"},
  .type = GF_OPTION_TYPE_BOOL
},
{ .key    = {NULL} },
};
```

The `fops` table, defined in `xlator.h`, is one of the most important pieces.
This table contains a pointer to each of the filesystem functions that your
translator might implement -- `open`, `read`, `stat`, `chmod`, and so on. There
are 82 such functions in all, but don't worry; any that you don't specify here
will be see as null and filled with defaults from `defaults.c` when your
translator is loaded. In this particular example, since `rot-13` is an
exceptionally simple translator, we only fill in two entries for `readv` and
`writev`.

There are actually two other tables, also required to have predefined names,
that are also used to find translator functions: `cbks` (which is empty in this
 snippet) and `dumpops` (which is missing entirely). The first of these specify
 entry points for when inodes are forgotten or file descriptors are released.
In other words, they're destructors for objects in which your translator might
 have an interest. Mostly you can ignore them, because the default behavior
handles even the simpler cases of translator-specific inode/fd context
automatically. However, if the context you attach is a complex structure
requiring complex cleanup, you'll need to supply these functions. As for
dumpops, that's just used if you want to provide functions to pretty-print
various structures in logs. I've never used it myself, though I probably
should. What's noteworthy here is that we don't even define dumpops. That's
because all of the functions that might use these dispatch functions will check
 for `xl->dumpops` being `NULL` before calling through it. This is in sharp
contrast to the behavior for `fops` and `cbks`, which *must* be present. If
they're not, translator loading will fail because these pointers are not
checked every time and if they're `NULL` then we'll segfault. That's why we
provide an empty definition for cbks; it's OK for the individual function
pointers to be NULL, but not for the whole table to be absent.

The last piece I'll cover today is options. As you can see, this is a table of
translator-specific option names and some information about their types.
GlusterFS actually provides a pretty rich set of types (`volume_option_type_t`
in `options.`h) which includes paths, translator names, percentages, and times
in addition to the obvious integers and strings. Also, the `volume_option_t`
structure can include information about alternate names, min/max/default
values, enumerated string values, and descriptions. We don't see any of these
here, so let's take a quick look at some more complex examples from afr.c and
then come back to `rot-13`.

```
{ .key  = {"data-self-heal-algorithm"},
  .type = GF_OPTION_TYPE_STR,
  .default_value = "",
  .description   = "Select between \"full\", \"diff\". The "
           "\"full\" algorithm copies the entire file from "
           "source to sink. The \"diff\" algorithm copies to "
           "sink only those blocks whose checksums don't match "
           "with those of source.",
  .value = { "diff", "full", "" }
},
{ .key  = {"data-self-heal-window-size"},
  .type = GF_OPTION_TYPE_INT,
  .min  = 1,
  .max  = 1024,
  .default_value = "1",
  .description = "Maximum number blocks per file for which "
         "self-heal process would be applied simultaneously."
},
```

When your translator is loaded, all of this information is used to parse the
options actually provided in the volfile, and then the result is turned into a
dictionary and stored as `xl->options`. This dictionary is then processed by
your init function, which you can see being looked up in the first code
fragment above. We're only going to look at a small part of the `rot-13`'s
init for now.

```
priv->decrypt_read = 1;
priv->encrypt_write = 1;

data = dict_get (this->options, "encrypt-write");
if (data) {
    if (gf_string2boolean (data->data, &priv->encrypt_write
        == -1) {
        gf_log (this->name, GF_LOG_ERROR,
            "encrypt-write takes only boolean options");
        return -1;
    }
}
```

What we can see here is that we're setting some defaults in our priv structure,
then looking to see if an `encrypt-write` option was actually provided. If so,
we convert and store it. This is a pretty classic use of dict_get to fetch a
field from a dictionary, and of using one of many conversion functions in
`common-utils.c` to convert `data->data` into something we can use.

So far we've covered the basic of how a translator gets loaded, how we find its
various parts, and how we process its options. In my next Translator 101 post,
we'll go a little deeper into other things that init and its companion fini
might do, and how some other fields in our `xlator_t` structure (commonly
referred to as this) are commonly used.

`init`, `fini`, and private context
-----------------------------------

In the previous Translator 101 post, we looked at some of the dispatch tables
and options processing in a translator. This time we're going to cover the rest
 of the "shell" of a translator -- i.e. the other global parts not specific to
handling a particular request.

Let's start by looking at the relationship between a translator and its shared
library. At a first approximation, this is the relationship between an object
and a class in just about any object-oriented programming language. The class
defines behaviors, but has to be instantiated as an object to have any kind of
existence. In our case the object is an `xlator_t`. Several of these might be
created within the same daemon, sharing all of the same code through init/fini
and dispatch tables, but sharing *no data*. You could implement shared data (as
 static variables in your shared libraries) but that's strongly discouraged.
Every function in your shared library will get an `xlator_t` as an argument,
and should use it. This lack of class-level data is one of the points where
the analogy to common OOP systems starts to break down. Another place is the
complete lack of inheritance. Translators inherit behavior (code) from exactly
one shared library -- looked up and loaded using the `type` field in a volfile
`volume ... end-volume` block -- and that's it -- not even single inheritance,
no subclasses or superclasses, no mixins or prototypes, just the relationship
between an object and its class. With that in mind, let's turn to the init
function that we just barely touched on last time.

```
int32_t
init (xlator_t *this)
{
    data_t *data = NULL;
    rot_13_private_t *priv = NULL;

    if (!this->children || this->children->next) {
        gf_log ("rot13", GF_LOG_ERROR,
            "FATAL: rot13 should have exactly one child");
        return -1;
    }

    if (!this->parents) {
        gf_log (this->name, GF_LOG_WARNING,
            "dangling volume. check volfile ");
    }

    priv = GF_CALLOC (sizeof (rot_13_private_t), 1, 0);
    if (!priv)
        return -1;
```

At the very top, we see the function signature -- we get a pointer to the
`xlator_t` object that we're initializing, and we return an `int32_t` status.
As with most functions in the translator API, this should be zero to indicate
success. In this case it's safe to return -1 for failure, but watch out: in
dispatch-table functions, the return value means the status of the *function
call* rather than the *request*. A request error should be reflected as a
callback with a non-zero `op_re`t value, but the dispatch function itself
should still return zero. In fact, the handling of a non-zero return from a
dispatch function is not all that robust (we recently had a bug report in
HekaFS related to this) so it's something you should probably avoid
altogether. This only underscores the difference between dispatch functions
and `init`/`fini` functions, where non-zero returns *are* expected and handled
logically by aborting the translator setup. We can see that down at the
bottom, where we return -1 to indicate that we couldn't allocate our
private-data area (more about that later).

The first thing this init function does is check that the translator is being
set up in the right kind of environment. Translators are called by parents and
in turn call children. Some translators are "initial" translators that inject
requests into the system from elsewhere -- e.g. mount/fuse injecting requests
from the kernel, protocol/server injecting requests from the network. Those
translators don't need parents, but `rot-13` does and so we check for that.
Similarly, some translators are "final" translators that (from the perspective
of the current process) terminate requests instead of passing them on -- e.g.
`protocol/client` passing them to another node, `storage/posix` passing them to
a local filesystem. Other translators "multiplex" between multiple children --
 passing each parent request on to one (`cluster/dht`), some
(`cluster/stripe`), or all (`cluster/afr`) of those children. `rot-13` fits
into none of those categories either, so it checks that it has *exactly one*
child. It might be more convenient or robust if translator shared libraries
had standard variables describing these requirements, to be checked in a
consistent way by the translator-loading infrastructure itself instead of by
each separate init function, but this is the way translators work today.

The last thing we see in this fragment is allocating our private data area.
This can literally be anything we want; the infrastructure just provides the
priv pointer as a convenience but takes no responsibility for how it's used. In
 this case we're using `GF_CALLOC` to allocate our own `rot_13_private_t`
structure. This gets us all the benefits of GlusterFS's memory-leak detection
infrastructure, but the way we're calling it is not quite ideal. For one thing,
 the first two arguments -- from `calloc(3)` -- are kind of reversed. For
another, notice how the last argument is zero. That can actually be an
enumerated value, to tell the GlusterFS allocator *what* type we're
allocating. This can be very useful information for memory profiling and leak
detection, so it's recommended that you follow the example of any
x`xx-mem-types.h` file elsewhere in the source tree instead of just passing
zero here (even though that works).

To finish our tour of standard initialization/termination, let's look at the
end of `init` and the beginning of `fini`:

```
    this->private = priv;
    gf_log ("rot13", GF_LOG_DEBUG, "rot13 xlator loaded");
    return 0;
}

void
fini (xlator_t *this)
{
    rot_13_private_t *priv = this->private;

    if (!priv)
        return;
    this->private = NULL;
    GF_FREE (priv);
```

At the end of init we're just storing our private-data pointer in the `priv`
field of our `xlator_t`, then returning zero to indicate that initialization
succeeded. As is usually the case, our fini is even simpler. All it really has
to do is `GF_FREE` our private-data pointer, which we do in a slightly
roundabout way here. Notice how we don't even have a return value here, since
there's nothing obvious and useful that the infrastructure could do if `fini`
failed.

That's practically everything we need to know to get our translator through
loading, initialization, options processing, and termination. If we had defined
 no dispatch functions, we could actually configure a daemon to use our
translator and it would work as a basic pass-through from its parent to a
single child. In the next post I'll cover how to build the translator and
configure a daemon to use it, so that we can actually step through it in a
debugger and see how it all fits together before we actually start adding
functionality.

This Time For Real
------------------

In the first two parts of this series, we learned how to write a basic
translator skeleton that can get through loading, initialization, and option
processing. This time we'll cover how to build that translator, configure a
volume to use it, and run the glusterfs daemon in debug mode.

Unfortunately, there's not much direct support for writing new translators. You
can check out a GlusterFS tree and splice in your own translator directory, but
 that's a bit painful because you'll have to update multiple makefiles plus a
bunch of autoconf garbage. As part of the HekaFS project, I basically reverse
engineered the truly necessary parts of the translator-building process and
then pestered one of the Fedora glusterfs package maintainers (thanks
daMaestro!) to add a `glusterfs-devel` package with the required headers. Since
 then the complexity level in the HekaFS tree has crept back up a bit, but I
still remember the simple method and still consider it the easiest way to get
started on a new translator. For the sake of those not using Fedora, I'm going
to describe a method that doesn't depend on that header package. What it does
depend on is a GlusterFS source tree, much as you might have cloned from GitHub
 or the Gluster review site. This tree doesn't have to be fully built, but you
do need to run `autogen.sh` and configure in it. Then you can take the
following simple makefile and put it in a directory with your actual source.

```
# Change these to match your source code.
TARGET  = rot-13.so
OBJECTS = rot-13.o

# Change these to match your environment.
GLFS_SRC = /srv/glusterfs
GLFS_LIB = /usr/lib64
HOST_OS  = GF_LINUX_HOST_OS

# You shouldn't need to change anything below here.

CFLAGS  = -fPIC -Wall -O0 -g \
      -DHAVE_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
      -D$(HOST_OS) -I$(GLFS_SRC) -I$(GLFS_SRC)/contrib/uuid \
      -I$(GLFS_SRC)/libglusterfs/src
LDFLAGS = -shared -nostartfiles -L$(GLFS_LIB)
LIBS = -lglusterfs -lpthread

$(TARGET): $(OBJECTS)
    $(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)
```

Yes, it's still Linux-specific. Mea culpa. As you can see, we're sticking with
the `rot-13` example, so you can just copy the files from
`xlators/encryption/rot-13/src` in your GlusterFS tree to follow on. Type
`make` and you should be rewarded with a nice little `.so` file.

```
xlator_example$ ls -l rot-13.so
-rwxr-xr-x. 1 jeff jeff 40784 Nov 16 16:41 rot-13.so
```

Notice that we've built with optimization level zero and debugging symbols
included, which would not typically be the case for a packaged version of
GlusterFS. Let's put our version of `rot-13.so` into a slightly different file
on our system, so that it doesn't stomp on the installed version (not that
you'd ever want to use that anyway).

```
xlator_example# ls /usr/lib64/glusterfs/3git/xlator/encryption/
crypt.so crypt.so.0 crypt.so.0.0.0 rot-13.so rot-13.so.0
rot-13.so.0.0.0
xlator_example# cp rot-13.so \
    /usr/lib64/glusterfs/3git/xlator/encryption/my-rot-13.so
```

These paths represent the current Gluster filesystem layout, which is likely to
be deprecated in favor of the Fedora layout; your paths may vary. At this point
 we're ready to configure a volume using our new translator. To do that, I'm
going to suggest something that's strongly discouraged except during
development (the Gluster guys are going to hate me for this): write our own
volfile. Here's just about the simplest volfile you'll ever see.

```
volume my-posix
    type storage/posix
    option directory /srv/export
end-volume

volume my-rot13
    type encryption/my-rot-13
    subvolumes my-posix
end-volume
```

All we have here is a basic brick using `/srv/export` for its data, and then
an instance of our translator layered on top -- no client or server is
necessary for what we're doing, and the system will automatically push a
mount/fuse translator on top if there's no server translator. To try this out,
all we need is the following command (assuming the directories involved already
 exist).

```
xlator_example$ glusterfs --debug -f my.vol /srv/import
```

You should be rewarded with a whole lot of log output, including the text of
the volfile (this is very useful for debugging problems in the field). If you
go to another window on the same machine, you can see that you have a new
filesystem mounted.

```
~$ df /srv/import
Filesystem   1K-blocks     Used    Available Use% Mounted on
/srv/xlator_example/my.vol
             114506240     2706176 105983488   3% /srv/import
```

Just for fun, write something into a file in `/srv/import`, then look at the
corresponding file in `/srv/export` to see it all `rot-13`'ed for you.

```
~$ echo hello > /srv/import/a_file
~$ cat /srv/export/a_file
uryyb
```

There you have it -- functionality you control, implemented easily, layered on
top of local storage. Now you could start adding functionality -- real
encryption, perhaps -- and inevitably having to debug it. You could do that the
 old-school way, with `gf_log` (preferred) or even plain old `printf`, or you
could  run daemons under `gdb` instead. Alternatively, you could wait for the
next  Translator 101 post, where we'll be doing exactly that.

Debugging a Translator
----------------------

Now that we've learned what a translator looks like and how to build one, it's
time to run one and actually watch it work. The best way to do this is good
old-fashioned `gdb`, as follows (using some of the examples from last time).

```
xlator_example# gdb glusterfs
GNU gdb (GDB) Red Hat Enterprise Linux (7.2-50.el6)
...
(gdb) r --debug -f my.vol /srv/import
Starting program: /usr/sbin/glusterfs --debug -f my.vol /srv/import
...
[2011-11-23 11:23:16.495516] I [fuse-bridge.c:2971:fuse_init]
    0-glusterfs-fuse: FUSE inited with protocol versions:
    glusterfs 7.13 kernel 7.13
```

If you get to this point, your glusterfs client process is already running. You
can go to another window to see the mountpoint, do file operations, etc.

```
~# df /srv/import
Filesystem   1K-blocks     Used    Available Use% Mounted on
/root/xlator_example/my.vol
             114506240     2643968 106045568   3% /srv/import
~# ls /srv/import
a_file
~# cat /srv/import/a_file
hello
```

Now let's interrupt the process and see where we are.

```
^C
Program received signal SIGINT, Interrupt.
0x0000003a0060b3dc in pthread_cond_wait@@GLIBC_2.3.2 ()
                   from /lib64/libpthread.so.0
(gdb) info threads
  5 Thread 0x7fffeffff700 (LWP 27206)  0x0000003a002dd8c7
    in readv ()
    from /lib64/libc.so.6
  4 Thread 0x7ffff50e3700 (LWP 27205)  0x0000003a0060b75b
    in pthread_cond_timedwait@@GLIBC_2.3.2 ()
    from /lib64/libpthread.so.0
  3 Thread 0x7ffff5f02700 (LWP 27204)  0x0000003a0060b3dc
    in pthread_cond_wait@@GLIBC_2.3.2 ()
    from /lib64/libpthread.so.0
  2 Thread 0x7ffff6903700 (LWP 27203)  0x0000003a0060f245
    in sigwait ()
    from /lib64/libpthread.so.0
* 1 Thread 0x7ffff7957700 (LWP 27196)  0x0000003a0060b3dc
    in pthread_cond_wait@@GLIBC_2.3.2 ()
    from /lib64/libpthread.so.0
```

Like any non-toy server, this one has multiple threads. What are they all
doing? Honestly, even I don't know. Thread 1 turns out to be in
`event_dispatch_epoll`, which means it's the one handling all of our network
I/O. Note that with socket multi-threading patch this will change, with one
thread in `socket_poller` per connection. Thread 2 is in `glusterfs_sigwaiter`
which means signals will be isolated to that thread. Thread 3 is in
`syncenv_task`, so it's a worker process for synchronous requests such as
those used by the rebalance and repair code. Thread 4 is in
`janitor_get_next_fd`, so it's waiting for a chance to close no-longer-needed
file descriptors on the local filesystem. (I admit I had to look that one up,
BTW.) Lastly, thread 5 is in `fuse_thread_proc`, so it's the one fetching
requests from our FUSE interface. You'll often see many more threads than
this, but it's a pretty good basic set. Now, let's set a breakpoint so we can
actually watch a request.

```
(gdb) b rot13_writev
Breakpoint 1 at 0x7ffff50e4f0b: file rot-13.c, line 119.
(gdb) c
Continuing.
```

At this point we go into our other window and do something that will involve a write.

```
~# echo goodbye > /srv/import/another_file
(back to the first window)
[Switching to Thread 0x7fffeffff700 (LWP 27206)]

Breakpoint 1, rot13_writev (frame=0x7ffff6e4402c, this=0x638440,
    fd=0x7ffff409802c, vector=0x7fffe8000cd8, count=1, offset=0,
    iobref=0x7fffe8001070) at rot-13.c:119
119   rot_13_private_t *priv = (rot_13_private_t *)this->private;
```

Remember how we built with debugging symbols enabled and no optimization? That
will be pretty important for the next few steps. As you can see, we're in
`rot13_writev`, with several parameters.

* `frame` is our always-present frame pointer for this request. Also,
  `frame->local` will point to any local data we created and attached to the
  request ourselves.
* `this` is a pointer to our instance of the `rot-13` translator. You can examine
  it if you like to see the name, type, options, parent/children, inode table,
  and other stuff associated with it.
* `fd` is a pointer to a file-descriptor *object* (`fd_t`, not just a
  file-descriptor index which is what most people use "fd" for). This in turn
  points to an inode object (`inode_t`) and we can associate our own
  `rot-13`-specific data with either of these.
* `vector` and `count` together describe the data buffers for this write, which
  we'll get to in a moment.
* `offset` is the offset into the file at which we're writing.
* `iobref` is a buffer-reference object, which is used to track the life cycle
  of buffers containing read/write data. If you look closely, you'll notice that
  `vector[0].iov_base` points to the same address as `iobref->iobrefs[0].ptr`, which
  should give you some idea of the inter-relationships between vector and iobref.

OK, now what about that `vector`? We can use it to examine the data being
written, like this.

```
(gdb) p vector[0]
$2 = {iov_base = 0x7ffff7936000, iov_len = 8}
(gdb) x/s 0x7ffff7936000
0x7ffff7936000:     "goodbye\n"
```

It's not always safe to view this data as a string, because it might just as
well be binary data, but since we're generating the write this time it's safe
and convenient. With that knowledge, let's step through things a bit.

```
(gdb) s
120        if (priv->encrypt_write)
(gdb)
121            rot13_iovec (vector, count);
(gdb)
rot13_iovec (vector=0x7fffe8000cd8, count=1) at rot-13.c:57
57        for (i = 0; i < count; i++) {
(gdb)
58            rot13 (vector[i].iov_base, vector[i].iov_len);
(gdb)
rot13 (buf=0x7ffff7936000 "goodbye\n", len=8) at rot-13.c:45
45        for (i = 0; i < len; i++) {
(gdb)
46            if (buf[i] >= 'a' && buf[i] <= 'z')
(gdb)
47                buf[i] = 'a' + ((buf[i] - 'a' + 13) % 26);
```

Here we've stepped into `rot13_iovec`, which iterates through our vector
calling `rot13`, which in turn iterates through the characters in that chunk
doing the  `rot-13` operation if/as appropriate. This is pretty straightforward
stuff, so  let's skip to the next interesting bit.

```
(gdb) fin
Run till exit from #0  rot13 (buf=0x7ffff7936000 "goodbye\n",
    len=8) at rot-13.c:47
rot13_iovec (vector=0x7fffe8000cd8, count=1) at rot-13.c:57
57        for (i = 0; i < count; i++) {
(gdb) fin
Run till exit from #0  rot13_iovec (vector=0x7fffe8000cd8,
    count=1) at rot-13.c:57
rot13_writev (frame=0x7ffff6e4402c, this=0x638440,
    fd=0x7ffff409802c, vector=0x7fffe8000cd8, count=1,
    offset=0, iobref=0x7fffe8001070) at rot-13.c:123
123        STACK_WIND (frame,
(gdb) b 129
Breakpoint 2 at 0x7ffff50e4f35: file rot-13.c, line 129.
(gdb) b rot13_writev_cbk
Breakpoint 3 at 0x7ffff50e4db3: file rot-13.c, line 106.
(gdb) c
```

So we've set breakpoints on both the callback and the statement following the
`STACK_WIND`. Which one will we hit first?

```
Breakpoint 3, rot13_writev_cbk (frame=0x7ffff6e4402c,
    cookie=0x7ffff6e440d8, this=0x638440, op_ret=8, op_errno=0,
    prebuf=0x7fffefffeca0, postbuf=0x7fffefffec30)
    at rot-13.c:106
106  STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno,
                          prebuf, postbuf);
(gdb) bt
#0  rot13_writev_cbk (frame=0x7ffff6e4402c,
    cookie=0x7ffff6e440d8, this=0x638440, op_ret=8, op_errno=0,
    prebuf=0x7fffefffeca0, postbuf=0x7fffefffec30)
    at rot-13.c:106
#1  0x00007ffff52f1b37 in posix_writev (frame=0x7ffff6e440d8,
    this=<value optimized out>, fd=<value optimized out>,
    vector=<value optimized out>, count=1,
    offset=<value optimized out>, iobref=0x7fffe8001070)
    at posix.c:2217
#2  0x00007ffff50e513e in rot13_writev (frame=0x7ffff6e4402c,
    this=0x638440, fd=0x7ffff409802c, vector=0x7fffe8000cd8,
    count=1, offset=0, iobref=0x7fffe8001070) at rot-13.c:123
```

Surprise! We're in `rot13_writev_cbk` now, called (indirectly) while we're
still in `rot13_writev` before `STACK_WIND` returns (still at rot-13.c:123). If
 you did any request cleanup here, then you need to be careful about what you
do in the remainder of `rot13_writev` because data may have been freed etc.
It's tempting to say you should just do the cleanup in `rot13_writev` after
the `STACK_WIND,` but that's not valid because it's also possible that some
other translator returned without calling `STACK_UNWIND` -- i.e. before
`rot13_writev` is called, so then it would be the one getting null-pointer
errors instead. To put it another way, the callback and the return from
`STACK_WIND` can occur in either order or even simultaneously on different
threads. Even if you were to use reference counts, you'd have to make sure to
use locking or atomic operations to avoid races, and it's not worth it. Unless
you *really* understand the possible flows of control and know what you're
doing, it's better to do cleanup in the callback and nothing after
`STACK_WIND.`

At this point all that's left is a `STACK_UNWIND` and a return. The
`STACK_UNWIND` invokes our parent's completion callback, and in this case our
parent is FUSE so at that point the VFS layer is notified of the write being
complete. Finally, we return through several levels of normal function calls
until we come back to fuse_thread_proc, which waits for the next request.

So that's it. For extra fun, you might want to repeat this exercise by stepping
through some other call -- stat or setxattr might be good choices -- but you'll
 have to use a translator that actually implements those calls to see much
that's interesting. Then you'll pretty much know everything I knew when I
started writing my first for-real translators, and probably even a bit more. I
hope you've enjoyed this series, or at least found it useful, and if you have
any suggestions for other topics I should cover please let me know (via
comments or email, IRC or Twitter).

Other versions
--------------

Original author's site:

 * [Translator 101 - Setting the Stage](http://pl.atyp.us/hekafs.org/index.php/2011/11/translator-101-class-1-setting-the-stage/)

 * [Translator 101 - Init, Fini and Private Context](http://pl.atyp.us/hekafs.org/index.php/2011/11/translator-101-lesson-2-init-fini-and-private-context/)

 * [Translator 101 - This Time for Real](http://pl.atyp.us/hekafs.org/index.php/2011/11/translator-101-lesson-3-this-time-for-real/)

 * [Translator 101 - Debugging a Translator](http://pl.atyp.us/hekafs.org/index.php/2011/11/translator-101-lesson-4-debugging-a-translator/)

Gluster community site:

 * [Translators](http://www.gluster.org/community/documentation/index.php/Translators)
