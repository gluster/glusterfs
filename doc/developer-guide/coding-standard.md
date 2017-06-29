GlusterFS Coding Standards
==========================

Structure definitions should have a comment per member
------------------------------------------------------

Every member in a structure definition must have a comment about its purpose.
The comment should be descriptive without being overly verbose.  For pointer
members, lifecycle concerns for the pointed-to object should be noted.  For lock
members, the relationship between the lock member and the other members it
protects should be explicit.

*Bad:*

```
gf_lock_t   lock;           /* lock */
```

*Good:*

```
DBTYPE      access_mode;    /* access mode for accessing
                             * the databases, can be
                             * DB_HASH, DB_BTREE
                             * (option access-mode <mode>)
                             */
```

Use \_typename for struct tags and typename\_t for typedefs
---------------------------------------------------------

Being consistent here makes it possible to automate navigation from use of a
type to its true definition (not just the typedef).

*Bad:*

```
struct thing {...};
struct thing_t {...};
typedef struct _thing thing;
```

*Good:*

```
typedef struct _thing {...} thing_t;
```

No double underscores
---------------------

Identifiers beginning with double underscores are supposed to reserved for the
compiler.

http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf

When you need to define inner/outer functions, use a different prefix/suffix.

*Bad:*

```
void __do_something (void);

void
do_something (void)
{
        LOCK ();
        __do_something ();
        UNLOCK ();
}
```

*Good:*

```
void do_something_locked (void);
```

Only use safe pointers in initializers
----------------------------------------------------------

Some pointers, such as `this` in a fop function, can be assumed to be non-NULL.
However, other parameters and further-derived values might be NULL.

*Good:*

```
pid_t pid     = frame->root->pid;
```


*Bad:*

```
data_t *my_data = dict_get (xdata, "fubar");
```

No giant stack allocations
--------------------------

Synctasks have small finite stacks.  To avoid overflowing these stacks, avoid
allocating any large data structures on the stack.  Use dynamic allocation
instead.

*Bad:*

```
gf_boolean_t port_inuse[65536]; /* 256KB, this actually happened */
```


Validate all arguments to a function
------------------------------------

All pointer arguments to a function must be checked for `NULL`.
A macro named `GF_VALIDATE_OR_GOTO` (in `common-utils.h`)
takes two arguments; if the first is `NULL`, it writes a log message and
jumps to a label specified by the second aergument after setting errno
appropriately. There are several variants of this function for more
specific purposes, and their use is recommended.

*Bad:*

```
/* top of function */
ret = dict_get (xdata, ...)
```

*Good:*

```
/* top of function */
GF_VALIDATE_OR_GOTO(xdata,out);
ret = dict_get (xdata, ...)
```

Never rely on precedence of operators
-------------------------------------

Never write code that relies on the precedence of operators to execute
correctly.  Such code can be hard to read and someone else might not
know the precedence of operators as accurately as you do.  This includes
precedence of increment/decrement vs. field/subscript.  The only exceptions are
arithmetic operators (which have had defined precedence since before computers
even existed) and boolean negation.

*Bad:*

```
if (op_ret == -1 && errno != ENOENT)
++foo->bar      /* incrementing foo, or incrementing foo->bar? */
a && b || !c
```

*Good:*

```
if ((op_ret == -1) && (errno != ENOENT))
(++foo)->bar
++(foo->bar)
(a && b) || !c
a && (b || !c)
```

Use exactly matching types
--------------------------

Use a variable of the exact type declared in the manual to hold the
return value of a function. Do not use an 'equivalent' type.


*Bad:*

```
int len = strlen (path);
```

*Good:*

```
size_t len = strlen (path);
```

Avoid code such as `foo->bar->baz`; check every pointer
-------------------------------------------------------------

Do not write code that blindly follows a chain of pointer references. Any
pointer in the chain may be `NULL` and thus cause a crash. Verify that each
pointer is non-null before following it.  Even if `foo->bar` has been checked
and is known safe, repeating it can make code more verbose and less clear.

This rule includes `[]` as well as `->` because both dereference pointers.

*Bad:*

```
foo->bar->field1 = value1;
xyz = foo->bar->field2 + foo->bar->field3 * foo->bar->field4;
foo->bar[5].baz
```

*Good:*

```
my_bar = foo->bar;
if (!my_bar) ... return;
my_bar->field1 = value1;
xyz = my_bar->field2 + my_bar->field3 * my_bar->field4;
```

Document unchecked return values
----------------------------------------------------

In general, return values should be checked.  If a function is being called
for its side effects and the return value really doesn't matter, an explicit
cast to void is required (to keep static analyzers happy) and a comment is
recommended.

*Bad:*

```
close (fd);
do_important_thing ();
```

*Good (or at least OK):*

```
(void) sleep (1);
```

Gracefully handle failure of malloc (and other allocation functions)
--------------------------------------------------------------------

GlusterFS should never crash or exit due to lack of memory. If a
memory allocation fails, the call should be unwound and an error
returned to the user.

*Use result args and reserve the return value to indicate success or failure:*

The return value of every functions must indicate success or failure (unless
it is impossible for the function to fail --- e.g., boolean functions). If
the function needs to return additional data, it must be returned using a
result (pointer) argument.

*Bad:*

```
int32_t dict_get_int32 (dict_t *this, char *key);
```

*Good:*

```
int dict_get_int32 (dict_t *this, char *key, int32_t *val);
```

Always use the 'n' versions of string functions
-----------------------------------------------

Unless impossible, use the length-limited versions of the string functions.

*Bad:*

```
strcpy (entry_path, real_path);
```

*Good:*

```
strncpy (entry_path, real_path, entry_path_len);
```

No dead or commented code
-------------------------

There must be no dead code (code to which control can never be passed) or
commented out code in the codebase.

Function length or Keep functions small
---------------------------------------

We live in the UNIX-world where modules do one thing and do it well.
This rule should apply to our functions also. If a function is very long, try splitting it
into many little helper functions. The question is, in a coding
spree, how do we know a function is long and unreadable. One rule of
thumb given by Linus Torvalds is that, a function should be broken-up
if you have 4 or more levels of indentation going on for more than 3-4
lines.

*Example for a helper function:*
```
static int
same_owner (posix_lock_t *l1, posix_lock_t *l2)
{
        return ((l1->client_pid == l2->client_pid) &&
               (l1->transport  == l2->transport));
}
```

Define functions as static
--------------------------

Declare functions as static unless they're exposed via a module-level API for
use from other modules.

No nested functions
-------------------

Nested functions have proven unreliable, e.g. as callbacks in code that uses
ucontext (green) threads,

Use inline functions instead of macros whenever possible
--------------------------------------------------------

Inline functions enforce type safety; macros do not.  Use macros only for things
that explicitly need to be type-agnostic (e.g. cases where one might use
generics or templates in other languages), or that use other preprocessor
features such as `#` for stringification or `##` for token pasting.  In general,
"static inline" is the preferred form.

Avoid copypasta
---------------

Code that is copied and then pasted into multiple functions often creates
maintenance problems later, e.g. updating all but one instance for a subsequent
change.  If you find yourself copying the same "boilerplate" many places,
consider refactoring to use helper functions (including inline) or macros, or
code generation.

Ensure function calls wrap around after 80-columns
--------------------------------------------------

Place remaining arguments on the next line if needed.

Functions arguments and function definition
-------------------------------------------

Place all the arguments of a function definition on the same line
until the line goes beyond 80-cols. Arguments that extend beyind
80-cols should be placed on the next line.

Style issues
------------

### Brace placement

Use K&R/Linux style of brace placement for blocks.

*Good:*

```
int some_function (...)
{
        if (...) {
                /* ... */
        } else if (...) {
                /* ... */
        } else {
                /* ... */
        }

        do {
                /* ... */
        } while (cond);
}
```

### Indentation

Use *eight* spaces for indenting blocks. Ensure that your
file contains only spaces and not tab characters. You can do this
in Emacs by selecting the entire file (`C-x h`) and
running `M-x untabify`.

To make Emacs indent lines automatically by eight spaces, add this
line to your `.emacs`:

```
(add-hook 'c-mode-hook (lambda () (c-set-style "linux")))
```

### Comments

Write a comment before every function describing its purpose (one-line),
its arguments, and its return value. Mention whether it is an internal
function or an exported function.

Write a comment before every structure describing its purpose, and
write comments about each of its members.

Follow the style shown below for comments, since such comments
can then be automatically extracted by doxygen to generate
documentation.

*Good:*

```
/**
* hash_name -hash function for filenames
* @par:  parent inode number
* @name: basename of inode
* @mod:  number of buckets in the hashtable
*
* @return: success: bucket number
*          failure: -1
*
* Not for external use.
*/
```

### Indicating critical sections

To clearly show regions of code which execute with locks held, use
the following format:

```
pthread_mutex_lock (&mutex);
{
        /* code */
}
pthread_mutex_unlock (&mutex);
```

### Always use braces

Even around single statements.

*Bad:*

```
if (condition) action ();

if (condition)
        action ();
```

*Good:*

```
if (condition) {
        action ();
}
```

### Avoid multi-line conditionals

These can be hard to read and even harder to modify later.  Predicate functions
and helper variables are always better for maintainability.

*Bad:*

```
if ((thing1 && other_complex_condition (thing1, lots, of, args))
    || (!thing2 || even_more_complex_condition (thing2))
    || all_sorts_of_stuff_with_thing3) {
    return;
}

```

*Better:*

```
thing1_ok = predicate1 (thing1, lots, of, args
thing2_ok = predicate2 (thing2);
thing3_ok = predicate3 (thing3);

if (!thing1_ok || !thing2_ok || !thing3_ok) {
        return;
}
```

*Best:*

```
if (thing1 && other_complex_condition (thing1, lots, of, args)) {
        return;
}
if (!thing2 || even_more_complex_condition (thing2)) {
        /* Note potential for a different message here. */
        return;
}
if (all_sorts_of_stuff_with_thing3) {
        /* And here too. */
        return;
}
```

### Use 'const' liberally

If a value isn't supposed/expected to change, there's no cost to adding a
'const' keyword and it will help prevent violation of expectations.

### Avoid global variables (including 'static' auto variables)
Almost all state in Gluster is contextual and should be contained in the
appropriate structure reflecting its scope (e.g. call\_frame\_t, call\_stack\_t,
xlator\_t, glusterfs\_ctx\_t).  With dynamic loading and graph switches in play,
each global requires careful consideration of when it should be initialized or
reinitialized, when it might _accidentally_ be reinitialized, when its value
might become stale, and so on.  A few global variables are needed to serve as
'anchor points' for these structures, and more exceptions to the rule might be
approved in the future, but new globals should not be added to the codebase
without explicit approval.

## A skeleton fop function

This is the recommended template for any fop. In the beginning come the
initializations. After that, the 'success' control flow should be linear.  Any
error conditions should cause a `goto` to a label at the end.  By convention
this is 'out' if there is only one such label, but a cascade of such labels is
allowable to support multi-stage cleanup.  At that point, the code should detect
the error that has occurred and do appropriate cleanup.

```
int32_t
sample_fop (call_frame_t *frame, xlator_t *this, ...)
{
        char *            var1     = NULL;
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        DIR *             dir      = NULL;
        struct posix_fd * pfd      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);

        /* other validations */

        dir = opendir (...);

        if (dir == NULL) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "opendir failed on %s (%s)", loc->path,
                        strerror (op_errno));
                goto out;
        }

        /* another system call */
        if (...) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }

        /* ... */

 out:
        if (op_ret == -1) {

          /* check for all the cleanup that needs to be
             done */

                if (dir) {
                        closedir (dir);
                        dir = NULL;
                }

                if (pfd) {
                        FREE (pfd->path);
                        FREE (pfd);
                        pfd = NULL;
                }
        }

        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
}
```
