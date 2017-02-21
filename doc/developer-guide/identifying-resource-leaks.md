# Identifying Resource Leaks

Like most other pieces of software, GlusterFS is not perfect in how it manages
its resources like memory, threads and the like. Gluster developers try hard to
prevent leaking resources but releasing and unallocating the used structures.
Unfortunately every now and then some resource leaks are unintentionally added.

This document tries to explain a few helpful tricks to identify resource leaks
so that they can be addressed.


## Debug Builds

There are certain techniques used in GlusterFS that make it difficult to use
tools like Valgrind for memory leak detection. There are some build options
that make it more practical to use Valgrind and other tools. When running
Valgrind, it is important to have GlusterFS builds that contain the
debuginfo/symbols. Some distributions (try to) strip the debuginfo to get
smaller executables. Fedora and RHEL based distributions have sub-packages
called ...-debuginfo that need to be installed for symbol resolving.


### Memory Pools

By using memory pools, there are no allocation/freeing of single structures
needed. This improves performance, but also makes it impossible to track the
allocation and freeing of srtuctures.

It is possible to disable the use of memory pools, and use standard `malloc()`
and `free()` functions provided by the C library. Valgrind is then able to
track the allocated areas and verify if they have been free'd. In order to
disable memory pools, the Gluster sources needs to be configured with the
`--enable-debug` option:

```shell
./configure --enable-debug
```

When building RPMs, the `.spec` handles the `--with=debug` option too:

```shell
make dist
rpmbuild -ta --with=debug glusterfs-....tar.gz
```

### Dynamically Loaded xlators

Valgrind tracks the call chain of functions that do memory allocations. The
addresses of the functions are stored and before Valgrind exits the addresses
are resolved into human readable function names and offsets (line numbers in
source files). Because Gluster loads xlators dynamically, and unloads then
before exiting, Valgrind is not able to resolve the function addresses into
symbols anymore. Whenever this happend, Valgrind shows `???` in the output,
like

```
  ==25170== 344 bytes in 1 blocks are definitely lost in loss record 233 of 324
  ==25170==    at 0x4C29975: calloc (vg_replace_malloc.c:711)
  ==25170==    by 0x52C7C0B: __gf_calloc (mem-pool.c:117)
  ==25170==    by 0x12B0638A: ???
  ==25170==    by 0x528FCE6: __xlator_init (xlator.c:472)
  ==25170==    by 0x528FE16: xlator_init (xlator.c:498)
  ...
```

These `???` can be prevented by not calling `dlclose()` for unloading the
xlator. This will cause a small leak of the handle that was returned with
`dlopen()`, but for improved debugging this can be acceptible. For this and
other Valgrind features, a `--enable-valgrind` option is available to
`./configure`. When GlusterFS is built with this option, Valgrind will be able
to resolve the symbol names of the functions that do memory allocations inside
xlators.

```shell
./configure --enable-valgrind
```

When building RPMs, the `.spec` handles the `--with=valgrind` option too:

```shell
make dist
rpmbuild -ta --with=valgrind glusterfs-....tar.gz
```

## Running Valgrind against a single xlator

Debugging a single xlator is not trivial. But there are some tools to make it
easier. The `sink` xlator does not do any memory allocations itself, but
contains just enough functionality to mount a volume with only the `sink`
xlator. There is a little gfapi application under `tests/basic/gfapi/` in the
GlusterFS sources that can be used to run only gfapi and the core GlusterFS
infrastructure with the `sink` xlator. By extending the `.vol` file to load
more xlators, each xlator can be debugged pretty much separately (as long as
the xlators have no dependencies on each other). A basic Valgrind run with the
suitable configure options looks like this:

```shell
./autogen.sh
./configure --enable-debug --enable-valgrind
make && make install
cd tests/basic/gfapi/
make gfapi-load-volfile
valgrind ./gfapi-load-volfile sink.vol
```

Combined with other very useful options to Valgrind, the following execution
shows many more useful details:

```shell
valgrind \
        --fullpath-after= --leak-check=full --show-leak-kinds=all \
        ./gfapi-load-volfile sink.vol
```

Note that the `--fullpath-after=` option is left empty, this makes Valgrind
print the full path and filename that contains the functions:

```
==2450== 80 bytes in 1 blocks are definitely lost in loss record 8 of 60
==2450==    at 0x4C29975: calloc (/builddir/build/BUILD/valgrind-3.11.0/coregrind/m_replacemalloc/vg_replace_malloc.c:711)
==2450==    by 0x52C6F73: __gf_calloc (/usr/src/debug/glusterfs-3.11dev/libglusterfs/src/mem-pool.c:117)
==2450==    by 0x12F10CDA: init (/usr/src/debug/glusterfs-3.11dev/xlators/meta/src/meta.c:231)
==2450==    by 0x528EFD5: __xlator_init (/usr/src/debug/glusterfs-3.11dev/libglusterfs/src/xlator.c:472)
==2450==    by 0x528F105: xlator_init (/usr/src/debug/glusterfs-3.11dev/libglusterfs/src/xlator.c:498)
==2450==    by 0x52D9D8B: glusterfs_graph_init (/usr/src/debug/glusterfs-3.11dev/libglusterfs/src/graph.c:321)
...
```

In the above example, the `init` function in `xlators/meta/src/meta.c` does a
memory allocation on line 231. This memory is never free'd again, and hence
Valgrind logs this call stack. When looking in the code, it seems that the
allocation of `priv` is assigned to the `this->private` member of the
`xlator_t` structure. Because the allocation is done in `init()`, free'ing is
expected to happen in `fini()`. Both functions are shown below, with the
inclusion of the empty `fini()`:


```
226 int
227 init (xlator_t *this)
228 {
229         meta_priv_t *priv = NULL;
230 
231         priv = GF_CALLOC (sizeof(*priv), 1, gf_meta_mt_priv_t);
232         if (!priv)
233                 return -1;
234 
235         GF_OPTION_INIT ("meta-dir-name", priv->meta_dir_name, str, out);
236 
237         this->private = priv;
238 out:
239         return 0;
240 }
241 
242 
243 int
244 fini (xlator_t *this)
245 {
246         return 0;
247 }
```

In this case, the resource leak can be addressed by adding a single line to the
`fini()` function:

```
243 int
244 fini (xlator_t *this)
245 {
246         GF_FREE (this->private);
247         return 0;
248 }
```

Running the same Valgrind command and comparing the output will show that the
memory leak in `xlators/meta/src/meta.c:init` is not reported anymore.
