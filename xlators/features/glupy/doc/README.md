This is just the very start for a GlusterFS[1] meta-translator that will
allow translator code to be written in Python.  It's based on the standard
Python embedding (not extending) techniques, plus a dash of the ctypes module.
The interface is a pretty minimal adaptation of the dispatches and callbacks
from the C API[2] to Python, as follows:

* Dispatch functions and callbacks must be defined on an "xlator" class
  derived from gluster.Translator so that they'll be auto-registered with
  the C translator during initialization.

* For each dispatch or callback function you want to intercept, you define a
  Python function using the xxx\_fop\_t or xxx\_cbk\_t decorator.

* The arguments for each operation are different, so you'll need to refer to
  the C API.  GlusterFS-specific types are used (though only loc\_t is fully
  defined so far) and type correctness is enforced by ctypes.

* If you do intercept a dispatch function, it is your responsibility to call
  xxx\_wind (like STACK\_WIND in the C API but operation-specific) to pass
  the request to the next translator.  If you do not intercept a function, it
  will default the same way as for C (pass through to the same operation with
  the same arguments on the first child translator).

* If you intercept a callback function, it is your responsibility to call
  xxx\_unwind (like STACK\_UNWIND\_STRICT in the C API) to pass the request back
  to the caller.

So far only the lookup and create operations are handled this way, to support
the "negative lookup" example.  Now that the basic infrastructure is in place,
adding more functions should be very quick, though with that much boilerplate I
might pause to write a code generator.  I also plan to add structure
definitions and interfaces for some of the utility functions in libglusterfs
(especially those having to do with inode and fd context) in the fairly near
future.  Note that you can also use ctypes to get at anything not explicitly
exposed to Python already.

_If you're coming here because of the Linux Journal article, please note that
the code has evolved since that was written. The version that matches the
article is here:_

https://github.com/jdarcy/glupy/tree/4bbae91ba459ea46ef32f2966562492e4ca9187a

[1] http://www.gluster.org
[2] http://pl.atyp.us/hekafs.org/dist/xlator_api_2.html
