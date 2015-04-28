/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "mem-pool.h"
#include "logging.h"
#include "xlator.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <inttypes.h>
#include <string.h>
#include <cmocka_pbc.h>
#include <cmocka.h>

#ifndef assert_ptr_equal
#define assert_ptr_equal(a, b) \
    _assert_int_equal(cast_ptr_to_largest_integral_type(a), \
                      cast_ptr_to_largest_integral_type(b), \
                      __FILE__, __LINE__)
#endif

/*
 * memory header for gf_mem_set_acct_info
 */
typedef struct __attribute__((packed)) {
    uint32_t type;
    size_t size;
    xlator_t *xl;
    uint32_t header_magic;
    uint8_t pad[8];
} mem_header_t;

/*
 * Prototypes to private functions
 */
int
gf_mem_set_acct_info (xlator_t *xl, char **alloc_ptr, size_t size,
                      uint32_t type, const char *typestr);

/*
 * Helper functions
 */
static xlator_t *
helper_xlator_init(uint32_t num_types)
{
    xlator_t *xl;
    int i, ret;

    REQUIRE(num_types > 0);

    xl = test_calloc(1, sizeof(xlator_t));
    assert_non_null(xl);
    xl->mem_acct->num_types = num_types;
    xl->mem_acct = test_calloc (sizeof(struct mem_acct)
                                + sizeof(struct mem_acct_rec) * num_types);
    assert_non_null(xl->mem_acct);

    xl->ctx = test_calloc(1, sizeof(glusterfs_ctx_t));
    assert_non_null(xl->ctx);

    for (i = 0; i < num_types; i++) {
            ret = LOCK_INIT(&(xl->mem_acct->rec[i].lock));
            assert_int_equal(ret, 0);
    }

    ENSURE(num_types == xl->mem_acct->num_types);
    ENSURE(NULL != xl);

    return xl;
}

static int
helper_xlator_destroy(xlator_t *xl)
{
    int i, ret;

    for (i = 0; i < xl->mem_acct->num_types; i++) {
            ret = LOCK_DESTROY(&(xl->mem_acct->rec[i].lock));
            assert_int_equal(ret, 0);
    }

    free(xl->mem_acct->rec);
    free(xl->ctx);
    free(xl);
    return 0;
}

static void
helper_check_memory_headers( char *mem,
        xlator_t *xl,
        size_t size,
        uint32_t type)
{
    mem_header_t *p;

    p = (mem_header_t *)mem,
    assert_int_equal(p->type, type);
    assert_int_equal(p->size, size);
    assert_true(p->xl == xl);
    assert_int_equal(p->header_magic, GF_MEM_HEADER_MAGIC);
    assert_true(*(uint32_t *)(mem+sizeof(mem_header_t)+size) == GF_MEM_TRAILER_MAGIC);

}

/*
 * Tests
 */
static void
test_gf_mem_acct_enable_set(void **state)
{
    (void) state;
    glusterfs_ctx_t test_ctx;

    expect_assert_failure(gf_mem_acct_enable_set(NULL));

    memset(&test_ctx, 0, sizeof(test_ctx));
    assert_true(NULL == test_ctx.process_uuid);
    gf_mem_acct_enable_set((void *)&test_ctx);
    assert_true(1 == test_ctx.mem_acct_enable);
    assert_true(NULL == test_ctx.process_uuid);
}

static void
test_gf_mem_set_acct_info_asserts(void **state)
{
    xlator_t *xl;
    xlator_t xltest;
    char *alloc_ptr;
    size_t size;
    uint32_t type;

    memset(&xltest, 0, sizeof(xlator_t));
    xl = (xlator_t *)0xBADD;
    alloc_ptr = (char *)0xBADD;
    size = 8196;
    type = 0;


    // Check xl is NULL
    expect_assert_failure(gf_mem_set_acct_info(NULL, &alloc_ptr, size, type, ""));
    // Check xl->mem_acct = NULL
    expect_assert_failure(gf_mem_set_acct_info(&xltest, &alloc_ptr, 0, type, ""));
    // Check type <= xl->mem_acct->num_types
    type = 100;
    expect_assert_failure(gf_mem_set_acct_info(&xltest, &alloc_ptr, 0, type, ""));
    // Check alloc is NULL
    assert_int_equal(-1, gf_mem_set_acct_info(&xltest, NULL, size, type, ""));

    // Initialize xl
    xl = helper_xlator_init(10);

    // Test number of types
    type = 100;
    assert_true(NULL != xl->mem_acct);
    assert_true(type > xl->mem_acct->num_types);
    expect_assert_failure(gf_mem_set_acct_info(xl, &alloc_ptr, size, type, ""));

    helper_xlator_destroy(xl);
}

static void
test_gf_mem_set_acct_info_memory(void **state)
{
    xlator_t *xl;
    char *alloc_ptr;
    char *temp_ptr;
    size_t size;
    uint32_t type;
    const char *typestr = "TEST";

    size = 8196;
    type = 9;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_null(xl->mem_acct->rec[type].typestr);

    // Test allocation
    temp_ptr = test_calloc(1, size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE);
    assert_non_null(temp_ptr);
    alloc_ptr = temp_ptr;
    gf_mem_set_acct_info(xl, &alloc_ptr, size, type, typestr);

    //Check values
    assert_ptr_equal(typestr, xl->mem_acct->rec[type].typestr);
    assert_int_equal(xl->mem_acct->rec[type].size, size);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 1);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 1);
    assert_int_equal(xl->mem_acct->rec[type].max_size, size);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 1);

    // Check memory
    helper_check_memory_headers(temp_ptr, xl, size, type);

    // Check that alloc_ptr has been moved correctly
    // by gf_mem_set_acct_info
    {
        mem_header_t *p;

        p = (mem_header_t *)temp_ptr;
        p++;
        p->type = 1234;
        assert_int_equal(*(uint32_t *)alloc_ptr, p->type);
    }

    free(temp_ptr);
    helper_xlator_destroy(xl);
}

static void
test_gf_calloc_default_calloc(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;
    uint32_t type;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);
    will_return(__glusterfs_this_location, &xl);

    // Call __gf_calloc
    size = 1024;
    type = 3;
    mem = __gf_calloc(1, size, type, "3");
    assert_non_null(mem);
    memset(mem, 0x5A, size);

    // Check xl did not change
    assert_int_equal(xl->mem_acct->rec[type].size, 0);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 0);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 0);
    assert_int_equal(xl->mem_acct->rec[type].max_size, 0);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 0);

    free(mem);
    helper_xlator_destroy(xl);
}

static void
test_gf_calloc_mem_acct_enabled(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;
    uint32_t type;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);
    xl->ctx->mem_acct_enable = 1;

    // For line mem-pool.c:115 and mem-pool:118
    will_return_always(__glusterfs_this_location, &xl);

    // Call __gf_calloc
    size = 1024;
    type = 3;
    mem = __gf_calloc(1, size, type, "3");
    assert_non_null(mem);
    memset(mem, 0x5A, size);

    // Check xl values
    assert_int_equal(xl->mem_acct->rec[type].size, size);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 1);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 1);
    assert_int_equal(xl->mem_acct->rec[type].max_size, size);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 1);

    // Check memory
    helper_check_memory_headers(mem - sizeof(mem_header_t), xl, size, type);
    free(mem - sizeof(mem_header_t));
    helper_xlator_destroy(xl);
}

static void
test_gf_malloc_default_malloc(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;
    uint32_t type;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);
    will_return(__glusterfs_this_location, &xl);

    // Call __gf_malloc
    size = 1024;
    type = 3;
    mem = __gf_malloc(size, type, "3");
    assert_non_null(mem);
    memset(mem, 0x5A, size);

    // Check xl did not change
    assert_int_equal(xl->mem_acct->rec[type].size, 0);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 0);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 0);
    assert_int_equal(xl->mem_acct->rec[type].max_size, 0);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 0);

    free(mem);
    helper_xlator_destroy(xl);
}

static void
test_gf_malloc_mem_acct_enabled(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;
    uint32_t type;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);
    xl->ctx->mem_acct_enable = 1;

    // For line mem-pool.c:115 and mem-pool:118
    will_return_always(__glusterfs_this_location, &xl);

    // Call __gf_malloc
    size = 1024;
    type = 3;
    mem = __gf_malloc(size, type, "3");
    assert_non_null(mem);
    memset(mem, 0x5A, size);

    // Check xl values
    assert_int_equal(xl->mem_acct->rec[type].size, size);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 1);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 1);
    assert_int_equal(xl->mem_acct->rec[type].max_size, size);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 1);

    // Check memory
    helper_check_memory_headers(mem - sizeof(mem_header_t), xl, size, type);
    free(mem - sizeof(mem_header_t));
    helper_xlator_destroy(xl);
}

static void
test_gf_realloc_default_realloc(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;
    uint32_t type;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);
    will_return_always(__glusterfs_this_location, &xl);

    // Call __gf_malloc then realloc
    size = 10;
    type = 3;
    mem = __gf_malloc(size, type, "3");
    assert_non_null(mem);
    memset(mem, 0xA5, size);

    size = 1024;
    mem = __gf_realloc(mem, size);
    assert_non_null(mem);
    memset(mem, 0x5A, size);

    // Check xl did not change
    assert_int_equal(xl->mem_acct->rec[type].size, 0);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 0);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 0);
    assert_int_equal(xl->mem_acct->rec[type].max_size, 0);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 0);

    free(mem);
    helper_xlator_destroy(xl);
}

static void
test_gf_realloc_mem_acct_enabled(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;
    uint32_t type;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);
    xl->ctx->mem_acct_enable = 1;

    // For line mem-pool.c:115 and mem-pool:118
    will_return_always(__glusterfs_this_location, &xl);

    // Call __gf_malloc then realloc
    size = 1024;
    type = 3;
    mem = __gf_malloc(size, type, "3");
    assert_non_null(mem);
    memset(mem, 0xA5, size);

    size = 2048;
    mem = __gf_realloc(mem, size);
    assert_non_null(mem);
    memset(mem, 0x5A, size);

    // Check xl values
    //
    // :TODO: This is really weird.  I would have expected
    // xl to only have a size equal to that of the realloc
    // not to the realloc + the malloc.
    // Is this a bug?
    //
    assert_int_equal(xl->mem_acct->rec[type].size, size+1024);
    assert_int_equal(xl->mem_acct->rec[type].num_allocs, 2);
    assert_int_equal(xl->mem_acct->rec[type].total_allocs, 2);
    assert_int_equal(xl->mem_acct->rec[type].max_size, size+1024);
    assert_int_equal(xl->mem_acct->rec[type].max_num_allocs, 2);

    // Check memory
    helper_check_memory_headers(mem - sizeof(mem_header_t), xl, size, type);
    free(mem - sizeof(mem_header_t));
    helper_xlator_destroy(xl);
}

static void
test_gf_realloc_ptr(void **state)
{
    xlator_t *xl;
    void *mem;
    size_t size;

    // Initialize xl
    xl = helper_xlator_init(10);
    assert_int_equal(xl->ctx->mem_acct_enable, 0);

    // For line mem-pool.c:115 and mem-pool:118
    will_return_always(__glusterfs_this_location, &xl);

    // Tests according to the manpage for realloc

    // Like a malloc
    size = 1024;
    mem = __gf_realloc(NULL, size);
    assert_non_null(mem);
    memset(mem, 0xA5, size);

    // Like a free
    mem = __gf_realloc(mem, 0);
    assert_null(mem);

    // Now enable xl context
    xl->ctx->mem_acct_enable = 1;
    expect_assert_failure(__gf_realloc(NULL, size));

    helper_xlator_destroy(xl);
}

int main(void) {
    const struct CMUnitTest libglusterfs_mem_pool_tests[] = {
        cmocka_unit_test(test_gf_mem_acct_enable_set),
        cmocka_unit_test(test_gf_mem_set_acct_info_asserts),
        cmocka_unit_test(test_gf_mem_set_acct_info_memory),
        cmocka_unit_test(test_gf_calloc_default_calloc),
        cmocka_unit_test(test_gf_calloc_mem_acct_enabled),
        cmocka_unit_test(test_gf_malloc_default_malloc),
        cmocka_unit_test(test_gf_malloc_mem_acct_enabled),
        cmocka_unit_test(test_gf_realloc_default_realloc),
        cmocka_unit_test(test_gf_realloc_mem_acct_enabled),
        cmocka_unit_test(test_gf_realloc_ptr),
    };

    return cmocka_run_group_tests(libglusterfs_mem_pool_tests, NULL, NULL);
}
