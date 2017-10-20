/*
  Copyright (c) 2008-2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-common.h"
#include "logging.h"
#include "xlator.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka_pbc.h>
#include <cmocka.h>

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
                                + sizeof(struct mem_acct_rec) + num_types);
    assert_non_null(xl->mem_acct);

    xl->ctx = test_calloc(1, sizeof(glusterfs_ctx_t));
    assert_non_null(xl->ctx);

    for (i = 0; i < num_types; i++) {
            ret = LOCK_INIT(&(xl->mem_acct.rec[i].lock));
            assert_false(ret);
    }

    ENSURE(num_types == xl->mem_acct.num_types);
    ENSURE(NULL != xl);

    return xl;
}

static int
helper_xlator_destroy(xlator_t *xl)
{
    int i, ret;

    for (i = 0; i < xl->mem_acct.num_types; i++) {
            ret = LOCK_DESTROY(&(xl->mem_acct.rec[i].lock));
            assert_int_equal(ret, 0);
    }

    free(xl->mem_acct.rec);
    free(xl->ctx);
    free(xl);
    return 0;
}

/*
 * Unit tests
 */
static void
test_dht_layout_new(void **state)
{
    xlator_t *xl;
    dht_layout_t *layout;
    dht_conf_t   *conf;
    int cnt;

    expect_assert_failure(dht_layout_new(NULL, 0));
    expect_assert_failure(dht_layout_new((xlator_t *)0x12345, -1));
    xl = helper_xlator_init(10);

    // xl->private is NULL
    assert_null(xl->private);
    cnt = 100;
    layout = dht_layout_new(xl, cnt);
    assert_non_null(layout);
    assert_int_equal(layout->type, DHT_HASH_TYPE_DM);
    assert_int_equal(layout->cnt, cnt);
    assert_int_equal(GF_ATOMIC_GET (layout->ref), 1);
    assert_int_equal(layout->gen, 0);
    assert_int_equal(layout->spread_cnt, 0);
    free(layout);

    // xl->private is not NULL
    cnt = 110;
    conf = (dht_conf_t *)test_calloc(1, sizeof(dht_conf_t));
    assert_non_null(conf);
    conf->dir_spread_cnt = 12345;
    conf->gen = -123;
    xl->private = conf;

    layout = dht_layout_new(xl, cnt);
    assert_non_null(layout);
    assert_int_equal(layout->type, DHT_HASH_TYPE_DM);
    assert_int_equal(layout->cnt, cnt);
    assert_int_equal(GF_ATOMIC_GET (layout->ref), 1);
    assert_int_equal(layout->gen, conf->gen);
    assert_int_equal(layout->spread_cnt, conf->dir_spread_cnt);
    free(layout);

    free(conf);
    helper_xlator_destroy(xl);
}

int main(void) {
    const struct CMUnitTest xlator_dht_layout_tests[] = {
        unit_test(test_dht_layout_new),
    };

    return cmocka_run_group_tests(xlator_dht_layout_tests, NULL, NULL);
}
