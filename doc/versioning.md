Versioning
==========

### current

The number of the current interface exported by the library. A current value
of '1', means that you are calling the interface exported by this library
interface 1.

### revision

The implementation number of the most recent interface exported by this library.
In this case, a revision value of `0` means that this is the first
implementation of the interface.

If the next release of this library exports the same interface, but has a
different implementation (perhaps some bugs have been fixed), the revision
number will be higher, but current number will be the same. In that case, when
given a choice, the library with the highest revision will always be used by
the runtime loader.

### age

The number of previous additional interfaces supported by this library. If age
were '2', then this library can be linked into executables which were built with
a release of this library that exported the current interface number, current,
or any of the previous two interfaces. By definition age must be less than or
equal to current. At the outset, only the first ever interface is implemented,
so age can only be `0'.

For every release of the library `-version-info` argument needs to be set
correctly depending on any interface changes you have made.

This is quite straightforward when you understand what the three numbers mean:

If you have changed any of the sources for this library, the revision number
must be incremented. This is a new revision of the current interface. If the
interface has changed, then current must be incremented, and revision reset
to '0'.

This is the first revision of a new interface. If the new interface is a
superset of the previous interface (that is, if the previous interface has not
been broken by the changes in this new release), then age must be incremented.
This release is backwards compatible with the previous release.
