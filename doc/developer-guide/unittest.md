# Unit Tests in GlusterFS

## Overview
[Art-of-unittesting][definitionofunittest] provides a good definition for unit tests.  A good unit test is:

* Able to be fully automated
* Has full control over all the pieces running (Use mocks or stubs to achieve this isolation when needed)
* Can be run in any order  if part of many other tests
* Runs in memory (no DB or File access, for example)
* Consistently returns the same result (You always run the same test, so no random numbers, for example. save those for integration or range tests)
* Runs fast
* Tests a single logical concept in the system
* Readable
* Maintainable
* Trustworthy (when you see its result, you don’t need to debug the code just to be sure)

## cmocka
GlusterFS unit test framework is based on [cmocka][].  cmocka provides
developers with methods to isolate and test modules written in C language.  It
also provides integration with Jenkins by providing JUnit XML compliant unit
test results.

cmocka

## Running Unit Tests
To execute the unit tests, all you need is to type `make check`.  Here is a step-by-step example assuming you just cloned a GlusterFS tree:

```
$ ./autogen.sh
$ ./configure --enable-debug
$ make check
```

Sample output:

```
PASS: mem_pool_unittest
============================================================================
Testsuite summary for glusterfs 3git
============================================================================
# TOTAL: 1
# PASS:  1
# SKIP:  0
# XFAIL: 0
# FAIL:  0
# XPASS: 0
# ERROR: 0
============================================================================
```

In this example, `mem_pool_unittest` has multiple tests inside, but `make check` assumes that the program itself is the test, and that is why it only shows one test.  Here is the output when we run `mem_pool_unittest` directly:

```
$ ./libglusterfs/src/mem_pool_unittest
[==========] Running 10 test(s).
[ RUN      ] test_gf_mem_acct_enable_set
Expected assertion data != ((void *)0) occurred
[       OK ] test_gf_mem_acct_enable_set
[ RUN      ] test_gf_mem_set_acct_info_asserts
Expected assertion xl != ((void *)0) occurred
Expected assertion size > ((4 + sizeof (size_t) + sizeof (xlator_t *) + 4 + 8) + 8) occurred
Expected assertion type <= xl->mem_acct.num_types occurred
[       OK ] test_gf_mem_set_acct_info_asserts
[ RUN      ] test_gf_mem_set_acct_info_memory
[       OK ] test_gf_mem_set_acct_info_memory
[ RUN      ] test_gf_calloc_default_calloc
[       OK ] test_gf_calloc_default_calloc
[ RUN      ] test_gf_calloc_mem_acct_enabled
[       OK ] test_gf_calloc_mem_acct_enabled
[ RUN      ] test_gf_malloc_default_malloc
[       OK ] test_gf_malloc_default_malloc
[ RUN      ] test_gf_malloc_mem_acct_enabled
[       OK ] test_gf_malloc_mem_acct_enabled
[ RUN      ] test_gf_realloc_default_realloc
[       OK ] test_gf_realloc_default_realloc
[ RUN      ] test_gf_realloc_mem_acct_enabled
[       OK ] test_gf_realloc_mem_acct_enabled
[ RUN      ] test_gf_realloc_ptr
Expected assertion ((void *)0) != ptr occurred
[       OK ] test_gf_realloc_ptr
[==========] 10 test(s) run.
[  PASSED  ] 10 test(s).
[  FAILED  ] 0 test(s).
[  REPORT  ] Created libglusterfs_mem_pool_xunit.xml report
```


## Writing Unit Tests

### Enhancing your C functions

#### Programming by Contract
Add the following to your C file:

```c
#include <cmocka_pbc.h>
```

```c
/*
 * Programming by Contract is a programming methodology
 * which binds the caller and the function called to a
 * contract. The contract is represented using Hoare Triple:
 *      {P} C {Q}
 * where {P} is the precondition before executing command C,
 * and {Q} is the postcondition.
 *
 * See also:
 * http://en.wikipedia.org/wiki/Design_by_contract
 * http://en.wikipedia.org/wiki/Hoare_logic
 * http://dlang.org/dbc.html
 */
 #ifndef CMOCKERY_PBC_H_
#define CMOCKERY_PBC_H_

#if defined(UNIT_TESTING) || defined (DEBUG)

#include <assert.h>

/*
 * Checks caller responsibility against contract
 */
#define REQUIRE(cond) assert(cond)

/*
 * Checks function reponsability against contract.
 */
#define ENSURE(cond) assert(cond)

/*
 * While REQUIRE and ENSURE apply to functions, INVARIANT
 * applies to classes/structs.  It ensures that intances
 * of the class/struct are consistent. In other words,
 * that the instance has not been corrupted.
 */
#define INVARIANT(invariant_fnc) do{ (invariant_fnc) } while (0);

#else
#define REQUIRE(cond) do { } while (0);
#define ENSURE(cond) do { } while (0);
#define INVARIANT(invariant_fnc) do{ } while (0);

#endif /* defined(UNIT_TESTING) || defined (DEBUG) */
#endif /* CMOCKERY_PBC_H_ */
```

##### Example
This is an _extremely_ simple example:

```c
int divide (int n, int d)
{
    int ans;

    REQUIRE(d != 0);

    ans = n / d;

    // As code is added to this function throughout its lifetime,
    // ENSURE will assert that data will be returned
    // according to the contract.  Again this is an
    // extremely simple example. :-D
    ENSURE( ans == (n / d) );

    return ans;
}

```

##### Important Note
`REQUIRE`, `ENSURE`, and `INVARIANT` are only available when `DEBUG` or `UNIT_TESTING` are set in the CFLAGS.  You must pass `--enable-debug` to `./configure` to enable PBC on your non-unittest builds.

#### Overriding functions
Cmockery2 provides its own memory allocation functions which check for buffer overrun and memory leaks.  The following header file must be included **last** to be able to override any of the memory allocation functions:

```c
#include <cmocka.h>
```

This file will only take effect with the `UNIT_TESTING` CFLAG is set.

### Creating a unit test
Once you identify the C file you would like to test, first create a `unittest` directory under the directory where the C file is located.  This will isolate the unittests to a different directory.

Next, you need to edit the `Makefile.am` file in the directory where your C file is located.  Initialize the
`Makefile.am` if it does not already have the following sections:

```
#### UNIT TESTS #####
CLEANFILES += *.gcda *.gcno *_xunit.xml
noinst_PROGRAMS =
TESTS =
```

Now you can add the following for each of the unit tests that you would like to build:

```
### UNIT TEST xxx_unittest ###
xxx_unittest_CPPFLAGS = $(xxx_CPPFLAGS)
xxx_unittest_SOURCES = xxx.c \
                       unittest/xxx_unittest.c
xxx_unittest_CFLAGS = $(UNITTEST_CFLAGS)
xxx_unittest_LDFLAGS = $(UNITTEST_LDFLAGS)
noinst_PROGRAMS += xxx_unittest
TESTS += xxx_unittest
```

Where `xxx` is the name of your C file. For example, look at `libglusterfs/src/Makefile.am`.

Copy the simple unit test from the [cmocka API][cmockaapi] to `unittest/xxx_unittest.c`.  If you would like to see an example of a unit test, please refer to `libglusterfs/src/unittest/mem_pool_unittest.c`.

#### Mocking
You may see that the linker will complain about missing functions needed by the C file you would like to test.  Identify the required functions, then place their stubs in a file called `unittest/xxx_mock.c`, then include this file in `Makefile.am` in `xxx_unittest_SOURCES`.  This will allow you to you Cmockery2's mocking functions.

#### Running the unit test
You can type `make` in the directory where the C file is located.  Once you built it and there are no errors, you can execute the test either by directly executing the program (in our example above it is called `xxx_unittest` ), or by running `make check`.

#### Debugging
Sometimes you may need to debug your unit test.  To do that, you will have to point `gdb` to the binary which is located in the same directory as the source.  For example, you can do the following from the root of the source tree to debug `mem_pool_unittest`:

```
$ gdb libglusterfs/src/mem_pool_unittest
```


[cmocka]: https://cmocka.org
[definitionofunittest]: http://artofunittesting.com/definition-of-a-unit-test/
[cmockapi]: https://api.cmocka.org
