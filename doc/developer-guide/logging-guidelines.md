# Guidelines on using the logging framework within a component

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

There are 3 interfaces provided to log messages:

1. GF_LOG* structured message interface

   All new messages should be defined using this interface. More details
   about it in the next section.

2. gf_msg* interface

   This interface is deprecated now. New log messages should use the
   new structured interface.

3. gf_log* interface

   This interface was deprecated long ago and it must not be used
   anymore.

## Structured log messages

This interface is designed to be easy to use, flexible and consistent.

The main advantages are:

- **Centralized message definition**

  All messages are defined in a unique location. If a message text needs to be
  updated, only one place has to be changed, even if the same log message is
  used in many places.

- **Customizable list of additional data per message**

  Each message can contain a list of additional info that will be logged as
  part of the message itself. This extra data is:

  - **Declared once**

    It's defined as part of the centralized message definition itself

  - **Typed**

    Each value has a type that is checked by the C compiler at build time to
    ensure correctness.

  - **Enforced**

    Each extra data field needs to be specified when a message of that type
    is logged. If the fields passed when a message is logged doesn't match the
    definition, the compiler will generate an error. This way it's easy to
    identify all places where a message has been used and update them.

- **Better uniformity in data type representation**

  Each data types are represented in the same way in all messages, increasing
  the consistency of the logs.

- **Compile-time generation of messages**

  The text and the extra data is formatted at compile time to reduce run time
  cost.

- **All argument preparation is done only if the message will be logged**

  Data types that need some preprocessing to be logged, are not computed until
  we are sure that the message needs to be logged based on the current log
  level.

- **Very easy to use**

  Definition of messages and its utilization is quite simple. There are some
  predefined types, but it's easy to create new data types if needed.

- **Code auto-completion friendly**

  Once a message is defined, logging it is very simple when an IDE with code
  auto-completion is used. The code auto-completion will help to find the
  name of the message and the list of arguments it needs.

- **All extra overhead is optimally optimized by gcc/clang**

  The additional code and structures required to make all this possible are
  easily optimized by compilers, so resulting code is equivalent to directly
  logging the message.

### Definition of new messages

All messages at log level INFO or above need to be declared inside a header
file. They will be assigned a unique identifier that will appear in the logs
so that specific messages can be easily located even if the text description
changes.

For DEBUG and TRACE messages, we don't assign a unique identifier to them and
the message is defined in-place where it's used with a very similar format.

#### Creating a new component

If a new xlator or component is created that requires some messages, the first
thing to do is to reserve a component ID in file glusterfs/glfs-message-id.h.

This is done by adding a new `GLFS_MSGID_COMP()` entry at the end of the
`enum _msgid_comp`. A unique name and a number of blocks to reserve must
be specified (each block can contain up to 1000 messages).

Example:

> ```c
>     GLFS_MSGID_COMP(EXAMPLE, 1),
>     /* --- new segments for messages goes above this line --- */
>
>     GLFS_MSGID_END
> ```

Once created, a copy of glusterfs/template-component-messages.h can be used as
a starting point for the messages of the new component. Check the comments of
that file for more information, but basically you need to use the macro
`GLFS_COMPONENT()` before starting defining the messages.

Example:

> ```c
> GLFS_COMPONENT(EXAMPLE);
> ```

#### Creating new messages

Each message is automatically assigned a unique sequential number and it
should remain the same once created. This means that you must create new
messages at the end of the file, after any other message. This way the newly
created message will take the next free sequential id, without touching any
previously assigned id.

To define a message, the macro `GLFS_NEW()` must be used. It requires four
mandatory arguments:

1. The name of the component. This is the one created in the previous section.

2. The name of the message. This is the name to use when you want to log the
   message.

3. The text associated to the message. This must be a fixed string without
   any formatting.

4. The number of extra data fields to include to the message.

If there are extra data fields, for each field you must add field definition
inside the macro.

For debug and trace logs, messages are not predefined. Wherever a these
messages are used, the definition of the message itself is used instead of
the name of the message.

##### Field definitions

Each field consists of five arguments, written between parenthesis:

1. **Data type**

   This is a regular C type that will be used to manipulate the data. It can
   be anything valid.

2. **Field name**

   This is the name that will be used to reference the data and to show it in
   the log message. It must be a valid C identifier.

3. **Data source**

   This is only used for in-place messages. It's a simple piece of code to
   access the data. It can be just a variable name or something a bit more
   complex like a structure access or even a function call returning a value.

4. **Format string**

   This is a string representing the way in which this data will be shown in
   the log. It can be something as simple as '%u' or a bit more elaborated
   like '%d (%s)', depending on how we want to show something.

5. **Format data**

   This must be a list of expressions to generate each of the arguments needed
   for the format string. In most cases this will be just the name of the
   field, but it could be something else if the data needs to be processed.

6. **Preparation code**

   This is optional. If present it must contain any additional variable
   definition and code to prepare the format data.

Examples for message definitions:

> ```c
>     (uint32_t, value, , "%u", (value))
> ```
>
> ```c
>     (int32_t, error, , "%d (%s)", (error, strerror(error)))
> ```
>
> ```c
>     (uuid_t *, gfid, , "%s", (gfid_str),
>      char gfid_str[48]; uuid_unparse(*gfid, gfid_str))
> ```

Examples for in-place messages:

> ```c
>     (uint32_t, value, data->count, "%u", (value))
> ```
>
> ```c
>     (int32_t, error, errno, "%d (%s)", (error, strerror(error)))
> ```
>
> ```c
>     (uuid_t *, gfid, &inode->gfid, "%s", (gfid_str),
>      char gfid_str[48]; uuid_unparse(*gfid, gfid_str))
> ```

##### Predefined data types

Some macros are available to declare typical data types and make them easier
to use:

- Signed integers: `GLFS_INT(name [, src])`
- Unsigned integers: `GLFS_UINT(name [, src])`
- Errors:
  - Positive errors: `GLFS_ERR(name [, src])`
  - Negative errors: `GLFS_RES(name [, src])`
- Strings: `GLFS_STR(name [, src])`
- UUIDs: `GLFS_UUID(name [, src])`
- Pointers: `GLFS_PTR(name [, src])`

The `src` argument is only used for in-place messages.

This is a full example that defines a new message using the previous macros:

```c
GLFS_NEW(EXAMPLE, MSG_TEST, "This is a test message", 3,
    GLFS_UINT(number),
    GLFS_STR(name),
    GLFS_ERR(error)
)
```

This will generate a log message with the following format:

```c
"This is a test message <{number=%u}, {name='%s'}, {error=%d (%s)}>"
```

#### Logging messages

Once a message is defined, it can be logged using the following macros:

- `GF_LOG_C()`: log a critical message
- `GF_LOG_E()`: log an error message
- `GF_LOG_W()`: log a warning message
- `GF_LOG_I()`: log an info message
- `GF_LOG_D()`: log a debug message
- `GF_LOG_T()`: log a trace message

All macros receive a string, representing the domain of the log message. For
INFO or higher messages, the name of the messages is passed, including all
additional data between parenthesis. In case of DEBUG and TRACE messages, a
message definition follows.

Example:

> ```c
>     GF_LOG_I(this->name, MSG_TEST(10, "something", ENOENT));
> ```

The resulting logging message would be similar to this:

```c
"This is a test message <{number=10}, {name='something'}, {error=2 (File not found)}>"
```

A similar example with a debug message:

> ```c
>     GF_LOG_D(this->name, "Debug message",
>         GLFS_UINT(number, data->value),
>         GLFS_STR(name),
>         GLFS_ERR(error, op_errno)
>     );

Note that if the field name matches the source of the data as in the case of
the second field, the source argument can be omitted.

## Migration from older interfaces

Given the amount of existing messages, it's not feasible to migrate all of
them at once, so a special macro is provided to allow incremental migration
of existing log messages.

1. Migrate header file

   The first step is to update the header file where all message IDs are
   defined.

   - Initialize the component

     You need to add the `GLFS_COMPONENT()` macro at the beginning with the
     appropriate component name. This name can be found in the first argument
     of the existing `GLFS_MSGID()` macro.

   - Replace message definitions

     All existing messages inside `GLFS_MSGID()` need to be converted to:

     ```c
     GLFS_MIG(component, id, "", 0)
     ```

     Where `component` is the name of the component used in `GLFS_COMPONENT()`,
     and `id` is each of the existing IDs inside `GLFS_MSGID()`.

     This step will use the new way of defining messages, but is compatible
     with the old logging interface, so once this is done, the code should
     compile fine.

2. Migrate a message

   It's possible to migrate the messages one by one without breaking anything.

   For each message to migrate:

   - Choose one message.
   - Replace `GLFS_MIG` by `GLFS_NEW`.
   - Add a meaningful message text as the third argument.
   - Update the number of fields if necessary.
   - Add the required field definition.
   - Look for each instance of the log message in the code.
   - Replace the existing log macro by one of the `GF_LOG_*()` macros.
