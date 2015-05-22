Guidelines on using the logging framework within a component
============================================================
Gluster library libglusterfs.so provides message logging abstractions that
are intended to be used across all code/components within gluster.

There could be potentially 2 major cases how the logging infrastructure is
used,
  - A new gluster service daemon or end point is created
    - The service daemon infrastructure itself initlializes the logging
    infrastructure (i.e calling gf_log_init and related set functions)
      - See, glusterfsd.c:logging_init
    - Alternatively there could be a case where an end point service (say
    gfapi) may need to do the required initialization
    - This document does not (yet?) cover guidelines for these cases. Best
    bet would be to look at code in glusterfsd.c:logging_init (or equivalent)
    in case a need arises and you reach this document.
  - A new xlator or subcomponent is written as a part of the stack
    - Primarily in this case, the consumer of the logging APIs would only
    invoke an API to *log* a particular message at a certain severity
    - This document elaborates on this use of the message logging framework
    in this context

There are 2 interfaces provided to log messages,
1. The older gf_log* interface
2. The newer gf_msg* interface

1. Older _to_be_deprecated_ gf_log* interface
  - Not much documentation is provided for this interface here, as these are
  meant to be deprecated and all code should be moved to the newer gf_msg*
  interface

2. New gf_msg* interface
  - The set of interfaces provided by gf_msg are,
    - NOTE: It is best to consult logging.h for the latest interfaces, as
    this document may get out of sync with the code

    *gf_msg(dom, levl, errnum, msgid, fmt...)*
      - Used predominantly to log a message above TRACE and DEBUG levels
      - See further guidelines below

    *gf_msg_debug(dom, errnum, fmt...)*
    *gf_msg_trace(dom, errnum, fmt...)*
      - Above are handy shortcuts for TRACE and DEBUG level messages
      - See further guidelines below

    *gf_msg_callingfn(dom, levl, errnum, msgid, fmt...)*
      - Useful function when a backtrace to detect calling routines that
      reached this execution point needs to be logged in addition to the
      message
      - This is very handy for framework libraries or code, as there could
      be many callers to the same, and the real failure would need the call
      stack to determine who the caller actually was
      - A good example is the dict interfaces using this function to log who
      called, when a particular error/warning needs to be displayed
      - See further guidelines below

    *gf_msg_plain(levl, fmt...)*
    *gf_msg_plain_nomem(levl, msg)*
    *gf_msg_vplain(levl, fmt, va)*
    *gf_msg_backtrace_nomem*
      - The above interfaces are provided to log messages without any typical
      headers (like the time stamp, dom, errnum etc.). The primary users of
      the above interfaces are, when printing the final graph, or printing
      the configuration when a process is about dump core or abort, or
      printing the backtrace when a process recieves a critical signal
      - These interfaces should not be used outside the scope of the users
      above, unless you know what you are doing

    *gf_msg_nomem(dom, levl, size)*
      - Very crisp messages, throwing out file, function, line numbers, in
      addition to the passed in size
      - These are used in the memory allocation abstractions interfaces in
      gluster, when the allocation fails (hence a no-mem log message, so that
      further allocation is not attempted by the logging infrastructure)
      - If you are contemplating using these, then you know why and how

  - Guidelines for the various arguments to the interfaces
    **dom**
      - The domain from which this message appears, IOW for xlators it should
      be the name of the xlator (as in this->name)

      - The intention of this string is to distinguish from which
      component/xlator the message is being printed

      - This information can also be gleaned from the FILE:LINE:FUNCTION
      information printed in the message anyway, but is retained to provide
      backward compatability to the messages as seen by admins earlier

    **levl**
      - Consult logging.h:gf_loglevel_t for logging levels (they pretty much
      map to syslog levels)

    **errnum**
      - errno should be passed in here, if available, 0 otherwise. This auto
      enables the logging infrastructure to print (man 3) strerror form of the
      same at the end of the message in a consistent format

      - If any message is already adding the strerror as a parameter to the
      message, it is suggested/encouraged to remove the same and pass it as
      errnum

      - The intention is that, if all messages did this using errnum and not
      as a part of their own argument list, the output would look consistent
      for the admin and cross component developers when reading logs

    **msgid**
      - This is a unique message ID per message (which now is more a message
      ID per group of messages in the implementation)

      - Rules for generating this ID can be found in dht-messages.h (it used
      to be template-common-messages.h, but that template is not updated,
      this comment should be changed once that is done) and glfs-message-id.h

      - Every message that is *above* TRACE and DEBUG should get a message
      ID, as this helps generating message catalogs that can help admins to
      understand the context of messages better. Another intention is that
      automated message parsers could detect a class of message IDs and send
      out notifications on the same, rather than parse the message string
      itself (which if it changes, the input to the parser has to change, etc.
      and hence is better to retain message IDs)

      - Ok so if intention is not yet clear, look at journald MESSAGE ID
      motivation, as that coupled with ident/dom above is expected to provide
      us with similar advantages

      - Bottomline: Every message gets its own ID, in case a message is
      *not* DEBUG or TRACE and still is developer centric, a generic message
      ID per component *maybe* assigned to the same to provide ease of use
      of the API

    **fmt**
      - As in format argument of (man 3) printf
