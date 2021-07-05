#include <stdio.h>
#include <assert.h>
#include <glusterfs/gf-buf.h>

size_t
gf_required_string_size(char *str)
{
    return __gf_buf_roundup(strlen(str) + 1);
}

size_t
gf_used_string_size(char *str)
{
    return strlen(str) + 1;
}

unsigned
gf_heap_allocated(char *str)
{
    return gf_required_string_size(str) > GF_BUF_STACK_THRESHOLD;
}

#define gf_buf_assert_string(buf, _heap, _allocated, _used, _str)              \
    do {                                                                       \
        assert((buf)->heap == _heap);                                          \
        assert((buf)->allocated == _allocated);                                \
        assert((buf)->string == 1);                                            \
        assert((buf)->used == _used);                                          \
        assert(!strcmp((buf)->data, _str));                                    \
    } while (0)

#define gf_buf_assert_simple(buf, str)                                         \
    gf_buf_assert_string((buf), gf_heap_allocated(str),                        \
                         gf_required_string_size(str),                         \
                         gf_used_string_size(str), (str))

#define gf_buf_assert_data(buf, _heap, _allocated, _used, _data, _size)        \
    do {                                                                       \
        assert((buf)->heap == (_heap));                                        \
        assert((buf)->allocated == (_allocated));                              \
        assert((buf)->string == 0);                                            \
        assert((buf)->used == (_used));                                        \
        if ((_size))                                                           \
            assert(!memcmp((buf)->data, (_data), (_size)));                    \
    } while (0)

void
gf_buf_test_string_basic(void)
{
    char big[PATH_MAX];

    gf_buf_decl_string(buf0);
    gf_buf_decl_init_string(buf1, "test");
    gf_buf_decl_init_string(buf2, "longlonglonglonglonglonglonglong");

    gf_buf_assert_string(&buf0, 0, 0, 1, "");
    gf_buf_assert_string(&buf1, 0, 0, 5, "test");
    gf_buf_assert_string(&buf2, 0, 0, 33, "longlonglonglonglonglonglonglong");

    gf_buf_assign_string(&buf0, "abcdef");
    gf_buf_assert_simple(&buf0, "abcdef");

    gf_buf_assign_string(&buf1, "0123456789");
    gf_buf_assert_simple(&buf1, "0123456789");

    memset(big, 'a', PATH_MAX - 1);
    big[PATH_MAX - 1] = '\0';
    gf_buf_assign_string(&buf2, big);
    gf_buf_assert_simple(&buf2, big);

    gf_buf_clear_string(&buf0);
    gf_buf_assert_string(&buf0, gf_heap_allocated("abcdef"),
                         gf_required_string_size("abcdef"),
                         gf_used_string_size(""), "");

    gf_buf_clear_string(&buf1);
    gf_buf_assert_string(&buf1, gf_heap_allocated("0123456789"),
                         gf_required_string_size("0123456789"),
                         gf_used_string_size(""), "");

    gf_buf_clear_string(&buf2);
    gf_buf_assert_string(&buf2, gf_heap_allocated(big),
                         gf_required_string_size(big), gf_used_string_size(""),
                         "");
}

void
gf_buf_test_string_compact(void)
{
    gf_buf_decl_string(buf0);
    gf_buf_assert_string(&buf0, 0, 0, 1, "");
    gf_buf_compact(&buf0);
    gf_buf_assert_string(&buf0, 0, 0, 1, "");

    gf_buf_assign_string(&buf0, "test");
    gf_buf_assert_simple(&buf0, "test");

    gf_buf_assign_string(&buf0, "longlonglonglonglonglonglonglong");
    gf_buf_assert_simple(&buf0, "longlonglonglonglonglonglonglong");

    gf_buf_assign_string(&buf0, "0123456789");
    gf_buf_assert_string(
        &buf0, gf_heap_allocated("longlonglonglonglonglonglonglong"),
        gf_required_string_size("longlonglonglonglonglonglonglong"),
        gf_used_string_size("0123456789"), "0123456789");

    gf_buf_compact(&buf0);
    gf_buf_assert_simple(&buf0, "0123456789");

    gf_buf_assign_string(&buf0, "abcd");
    gf_buf_assert_string(&buf0, gf_heap_allocated("0123456789"),
                         gf_required_string_size("0123456789"),
                         gf_used_string_size("abcd"), "abcd");

    gf_buf_compact(&buf0);
    gf_buf_assert_simple(&buf0, "abcd");
}

void
gf_buf_test_string_copy(void)
{
    gf_buf_decl_string(buf0);
    gf_buf_decl_init_string(buf1, "test");
    gf_buf_decl_init_string(buf2, "longlonglonglonglonglonglonglong");

    gf_buf_copy(&buf0, &buf1);
    gf_buf_assert_simple(&buf0, "test");

    gf_buf_copy(&buf1, &buf2);
    gf_buf_assert_simple(&buf1, "longlonglonglonglonglonglonglong");

    gf_buf_copy(&buf2, &buf0);
    gf_buf_assert_simple(&buf2, "test");

    gf_buf_copy(&buf1, &buf2);
    gf_buf_assert_string(
        &buf1, gf_heap_allocated("longlonglonglonglonglonglonglong"),
        gf_required_string_size("longlonglonglonglonglonglonglong"),
        gf_used_string_size("test"), "test");
}

void
gf_buf_test_string_equal(void)
{
    gf_buf_decl_init_string(buf0, "test");
    gf_buf_decl_init_string(buf1, "test");
    gf_buf_decl_init_string(buf2, "abcd");
    gf_buf_decl_init_string(buf3, "testtest");
    gf_buf_decl_init_string(buf4, "longlonglonglonglonglonglonglong");

    assert(gf_buf_equal(&buf0, &buf1));
    assert(!gf_buf_equal(&buf0, &buf2));
    assert(!gf_buf_equal(&buf0, &buf3));
    assert(!gf_buf_equal(&buf0, &buf4));

    gf_buf_copy(&buf2, &buf0);
    assert(gf_buf_equal(&buf0, &buf2));

    gf_buf_copy(&buf0, &buf4);
    assert(gf_buf_equal(&buf0, &buf4));

    gf_buf_copy(&buf1, &buf4);
    assert(gf_buf_equal(&buf1, &buf4));

    gf_buf_copy(&buf2, &buf4);
    assert(gf_buf_equal(&buf2, &buf4));

    gf_buf_copy(&buf3, &buf4);
    assert(gf_buf_equal(&buf3, &buf4));

    assert(gf_buf_equal(&buf4, &buf4));
}

void
gf_buf_test_string_concat(void)
{
    char big0[PATH_MAX], big1[PATH_MAX], big2[PATH_MAX], out[PATH_MAX * 3];

    gf_buf_decl_string(buf0);
    gf_buf_decl_string(buf1);
    gf_buf_decl_init_string(buf2, "test");

    gf_buf_concat(&buf0, &buf1);
    gf_buf_assert_simple(&buf0, "");

    gf_buf_concat(&buf0, &buf2);
    gf_buf_assert_simple(&buf0, "test");

    gf_buf_assign_string(&buf0, "x");
    gf_buf_assert_string(&buf0, gf_heap_allocated("test"),
                         gf_required_string_size("test"),
                         gf_used_string_size("x"), "x");

    gf_buf_concat_string(&buf0, "yz");
    gf_buf_assert_string(&buf0, gf_heap_allocated("test"),
                         gf_required_string_size("test"),
                         gf_used_string_size("xyz"), "xyz");

    gf_buf_concat_string(&buf0, "0123456789");
    gf_buf_assert_simple(&buf0, "xyz0123456789");

    gf_buf_assign_string(&buf0, "abcd");
    gf_buf_assign_string(&buf1, "1234");
    gf_buf_assign_string(&buf2, "xyzt");
    gf_buf_concat(&buf0, &buf1);
    gf_buf_concat(&buf0, &buf2);
    gf_buf_assert_string(&buf0, gf_heap_allocated("xyz0123456789"),
                         gf_required_string_size("xyz0123456789"),
                         gf_used_string_size("abcd1234xyzt"), "abcd1234xyzt");

    memset(big0, 'a', PATH_MAX - 1);
    big0[PATH_MAX - 1] = '\0';
    memset(big1, 'b', PATH_MAX - 1);
    big1[PATH_MAX - 1] = '\0';
    memset(big2, 'c', PATH_MAX - 1);
    big2[PATH_MAX - 1] = '\0';
    out[0] = '\0';

    gf_buf_assign_string(&buf0, big0);
    gf_buf_assert_simple(&buf0, big0);

    gf_buf_assign_string(&buf1, big1);
    gf_buf_assert_simple(&buf1, big1);

    gf_buf_assign_string(&buf2, big2);
    gf_buf_assert_simple(&buf2, big2);

    strcat(out, big0);
    strcat(out, big1);
    strcat(out, big2);

    gf_buf_concat(&buf0, &buf1);
    gf_buf_concat(&buf0, &buf2);
    gf_buf_assert_simple(&buf0, out);
}

void
gf_buf_test_sprintf(void)
{
    char *p = "oops";
    int x = 123, y = 0xaaaabbbb;

    gf_buf_decl_null(buf0);
    gf_buf_decl_string(buf1);
    gf_buf_decl_init_string(buf2, "test");
    gf_buf_decl_init_string(buf3, "abcd0123");

    gf_buf_sprintf(&buf0, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf0, "x = 123 y = 0xaaaabbbb p = oops");

    gf_buf_sprintf(&buf1, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf1, "x = 123 y = 0xaaaabbbb p = oops");

    gf_buf_sprintf(&buf2, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf2, "x = 123 y = 0xaaaabbbb p = oops");

    gf_buf_assign_string(&buf3, "longlonglonglonglonglonglonglonglong");
    gf_buf_sprintf(&buf3, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_string(
        &buf3, gf_heap_allocated("longlonglonglonglonglonglonglonglong"),
        gf_required_string_size("longlonglonglonglonglonglonglonglong"),
        gf_used_string_size("x = 123 y = 0xaaaabbbb p = oops"),
        "x = 123 y = 0xaaaabbbb p = oops");

    assert(gf_buf_equal(&buf0, &buf1));
    assert(gf_buf_equal(&buf1, &buf2));
    assert(gf_buf_equal(&buf2, &buf3));

    gf_buf_reset(&buf0);
    gf_buf_reset(&buf1);
    gf_buf_reset(&buf2);
    gf_buf_reset(&buf3);

    gf_buf_sprintf(&buf0, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf0, "x = 123 y = 0xaaaabbbb p = oops");

    gf_buf_sprintf(&buf1, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf1, "x = 123 y = 0xaaaabbbb p = oops");

    gf_buf_sprintf(&buf2, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf2, "x = 123 y = 0xaaaabbbb p = oops");

    gf_buf_sprintf(&buf3, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf3, "x = 123 y = 0xaaaabbbb p = oops");

    assert(gf_buf_equal(&buf0, &buf1));
    assert(gf_buf_equal(&buf1, &buf2));
    assert(gf_buf_equal(&buf2, &buf3));
}

void
gf_buf_test_snprintf(void)
{
    char *p = "oops";
    int x = 123, y = 0xaaaabbbb;

    gf_buf_decl_null(buf0);
    gf_buf_decl_string(buf1);
    gf_buf_decl_init_string(buf2, "longlonglonglonglonglonglonglonglong");

    gf_buf_snprintf(&buf0, 8, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf0, "x = 123");

    gf_buf_snprintf(&buf1, 23, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf1, "x = 123 y = 0xaaaabbbb");

    gf_buf_snprintf(&buf2, 23, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf2, "x = 123 y = 0xaaaabbbb");

    assert(gf_buf_equal(&buf1, &buf2));

    gf_buf_reset(&buf0);
    gf_buf_reset(&buf1);
    gf_buf_reset(&buf2);

    gf_buf_snprintf(&buf0, 8, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf0, "x = 123");

    gf_buf_snprintf(&buf1, 23, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf1, "x = 123 y = 0xaaaabbbb");

    gf_buf_snprintf(&buf2, 23, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_assert_simple(&buf2, "x = 123 y = 0xaaaabbbb");

    assert(gf_buf_equal(&buf1, &buf2));
}

void
gf_buf_test_string_null(void)
{
    gf_buf_decl_null(buf0);
    gf_buf_decl_null(buf1);
    gf_buf_decl_null(buf2);
    gf_buf_decl_string(buf3);
    gf_buf_decl_init_string(buf4, "test");

    gf_buf_assign_string(&buf0, "abcd");
    gf_buf_copy(&buf1, &buf3);
    gf_buf_copy(&buf2, &buf4);

    gf_buf_assert_simple(&buf0, "abcd");
    gf_buf_assert_simple(&buf1, "");
    gf_buf_assert_simple(&buf2, "test");

    assert(gf_buf_equal(&buf1, &buf3));
    assert(gf_buf_equal(&buf2, &buf4));
}

void
gf_buf_test_string_copyout(void)
{
    char *p, *q, *r;

    gf_buf_decl_init_string(buf0, "abcd");
    gf_buf_decl_string(buf1);
    gf_buf_decl_null(buf2);

    p = gf_buf_copyout(&buf0);
    assert(!strcmp(p, "abcd"));
    GF_FREE(p);

    q = gf_buf_copyout(&buf1);
    assert(!strcmp(q, ""));
    GF_FREE(q);

    r = gf_buf_copyout(&buf2);
    assert(r == NULL);
}

void
gf_buf_test_data_basic(void)
{
    int i;
    char data0[] = {0xa0, 0xa1, 0xa2, 0xa3};
    char data1[] = {0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
                    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};
    char data2[32], data3[64], zero[64];

    memset(zero, 0, sizeof(zero));

    for (i = 0; i < sizeof(data2); i++)
        data2[i] = 0xc0 + i;

    for (i = 0; i < sizeof(data3); i++)
        data3[i] = 0xd0 + i;

    gf_buf_decl_init_data(buf0, data0, sizeof(data0));
    gf_buf_decl_init_data(buf1, data1, sizeof(data1));

    gf_buf_assert_data(&buf0, 0, 0, sizeof(data0), data0, sizeof(data0));
    gf_buf_assert_data(&buf1, 0, 0, sizeof(data1), data1, sizeof(data1));

    gf_buf_assign_data(&buf0, data2, sizeof(data2));
    gf_buf_assign_data(&buf1, data3, sizeof(data3));

    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(data2)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data2)), sizeof(data2), data2, sizeof(data2));
    gf_buf_assert_data(
        &buf1, __gf_buf_roundup(sizeof(data3)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data3)), sizeof(data3), data3, sizeof(data3));

    gf_buf_clear_data(&buf0);
    gf_buf_assert_data(&buf0,
                       __gf_buf_roundup(sizeof(data2)) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(sizeof(data2)), 0, zero, sizeof(data2));

    gf_buf_clear_data(&buf1);
    gf_buf_assert_data(&buf1,
                       __gf_buf_roundup(sizeof(data3)) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(sizeof(data3)), 0, zero, sizeof(data3));
}

void
gf_buf_test_data_compact(void)
{
    char small[8], medium[32], large[256], huge[32768];

    memset(small, 'a', sizeof(small));
    memset(medium, 'b', sizeof(medium));
    memset(large, 'c', sizeof(large));
    memset(huge, 'd', sizeof(huge));

    gf_buf_decl_null(buf0);

    gf_buf_assign_data(&buf0, huge, sizeof(huge));
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(huge)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(huge)), sizeof(huge), huge, sizeof(huge));

    /* Compact huge -> large */
    gf_buf_assign_data(&buf0, large, sizeof(large));
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(huge)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(huge)), sizeof(large), large, sizeof(large));
    gf_buf_compact(&buf0);
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(large)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(large)), sizeof(large), large, sizeof(large));

    /* Compact large -> medium */
    gf_buf_assign_data(&buf0, medium, sizeof(medium));
    gf_buf_assert_data(&buf0,
                       __gf_buf_roundup(sizeof(large)) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(sizeof(large)), sizeof(medium), medium,
                       sizeof(medium));
    gf_buf_compact(&buf0);
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(medium)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(medium)), sizeof(medium), medium,
        sizeof(medium));

    /* Compact medium -> small */
    gf_buf_assign_data(&buf0, small, sizeof(small));
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(medium)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(medium)), sizeof(small), small, sizeof(small));
    gf_buf_compact(&buf0);
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(small)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(small)), sizeof(small), small, sizeof(small));
}

void
gf_buf_test_data_copy(void)
{
    char data1[8], data2[64];

    memset(data1, 'a', sizeof(data1));
    memset(data2, 'b', sizeof(data2));

    gf_buf_decl_null(buf0);
    gf_buf_decl_init_data(buf1, data1, sizeof(data1));
    gf_buf_decl_init_data(buf2, data2, sizeof(data2));

    gf_buf_copy(&buf0, &buf1);
    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(data1)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data1)), sizeof(data1), data1, sizeof(data1));

    gf_buf_copy(&buf1, &buf2);
    gf_buf_assert_data(
        &buf1, __gf_buf_roundup(sizeof(data2)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data2)), sizeof(data2), data2, sizeof(data2));

    gf_buf_copy(&buf2, &buf0);
    gf_buf_assert_data(
        &buf2, __gf_buf_roundup(sizeof(data1)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data1)), sizeof(data1), data1, sizeof(data1));

    gf_buf_copy(&buf1, &buf2);
    gf_buf_assert_data(
        &buf1, __gf_buf_roundup(sizeof(data2)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data2)), sizeof(data1), data1, sizeof(data1));
}

void
gf_buf_test_data_equal(void)
{
    char data0[4], data1[4], data2[4], data3[8], data4[64];

    memset(data0, 0xa, sizeof(data0));
    memset(data1, 0xa, sizeof(data1));
    memset(data2, 0xb, sizeof(data2));
    memset(data3, 0xc, sizeof(data3));
    memset(data4, 0xd, sizeof(data4));

    gf_buf_decl_init_data(buf0, data0, sizeof(data0));
    gf_buf_decl_init_data(buf1, data1, sizeof(data1));
    gf_buf_decl_init_data(buf2, data2, sizeof(data2));
    gf_buf_decl_init_data(buf3, data3, sizeof(data3));
    gf_buf_decl_init_data(buf4, data4, sizeof(data4));

    assert(gf_buf_equal(&buf0, &buf1));
    assert(!gf_buf_equal(&buf0, &buf2));
    assert(!gf_buf_equal(&buf0, &buf3));
    assert(!gf_buf_equal(&buf0, &buf4));

    gf_buf_copy(&buf2, &buf0);
    assert(gf_buf_equal(&buf0, &buf2));

    gf_buf_copy(&buf0, &buf4);
    assert(gf_buf_equal(&buf0, &buf4));

    gf_buf_copy(&buf1, &buf4);
    assert(gf_buf_equal(&buf1, &buf4));

    gf_buf_copy(&buf2, &buf4);
    assert(gf_buf_equal(&buf2, &buf4));

    gf_buf_copy(&buf3, &buf4);
    assert(gf_buf_equal(&buf3, &buf4));

    assert(gf_buf_equal(&buf4, &buf4));
}

void
gf_buf_test_data_concat()
{
    size_t size;
    char ignored[8];
    char data0[8], data1[32], data2[256], data3[32768],
        total[8 + 32 + 256 + 32768];

    memset(ignored, 0x0, sizeof(ignored));

    memset(data0, 0xa, sizeof(data0));
    memset(data1, 0xb, sizeof(data1));
    memset(data2, 0xc, sizeof(data2));
    memset(data3, 0xd, sizeof(data3));

    gf_buf_decl_init_data(buf0, data0, sizeof(data0));
    gf_buf_decl_init_data(buf1, data1, sizeof(data1));
    gf_buf_decl_init_data(buf2, data2, sizeof(data2));
    gf_buf_decl_init_data(buf3, data3, sizeof(data3));

    memcpy(total, data0, sizeof(data0));
    memcpy(total + sizeof(data0), data1, sizeof(data1));
    gf_buf_concat(&buf0, &buf1);
    size = sizeof(data0) + sizeof(data1);
    gf_buf_assert_data(&buf0, __gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(size), size, total, size);

    memcpy(total + sizeof(data0) + sizeof(data1), data2, sizeof(data2));
    gf_buf_concat(&buf0, &buf2);
    size = sizeof(data0) + sizeof(data1) + sizeof(data2);
    gf_buf_assert_data(&buf0, __gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(size), size, total, size);

    memcpy(total + sizeof(data0) + sizeof(data1) + sizeof(data2), data3,
           sizeof(data3));
    gf_buf_concat(&buf0, &buf3);
    size = sizeof(data0) + sizeof(data1) + sizeof(data2) + sizeof(data3);
    gf_buf_assert_data(&buf0, __gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(size), size, total, size);

    gf_buf_clear_data(&buf0);
    gf_buf_assert_data(&buf0, __gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(size), 0, ignored, 0);
    gf_buf_clear_data(&buf1);
    gf_buf_assert_data(&buf1, GF_BUF_ROUNDUP > GF_BUF_STACK_THRESHOLD,
                       GF_BUF_ROUNDUP, 0, ignored, 0);

    gf_buf_concat(&buf0, &buf1);
    gf_buf_assert_data(&buf0, __gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(size), 0, ignored, 0);

    gf_buf_compact(&buf0);
    gf_buf_assert_data(&buf0, GF_BUF_ROUNDUP > GF_BUF_STACK_THRESHOLD,
                       GF_BUF_ROUNDUP, 0, ignored, 0);
}

void
gf_buf_test_data_null()
{
    char data0[4], data3[16], data4[64];

    memset(data0, 0xa, sizeof(data0));
    memset(data3, 0xb, sizeof(data3));
    memset(data4, 0xc, sizeof(data4));

    gf_buf_decl_null(buf0);
    gf_buf_decl_null(buf1);
    gf_buf_decl_null(buf2);
    gf_buf_decl_init_data(buf3, data3, sizeof(data3));
    gf_buf_decl_init_data(buf4, data4, sizeof(data4));

    gf_buf_assign_data(&buf0, data0, sizeof(data0));
    gf_buf_copy(&buf1, &buf3);
    gf_buf_copy(&buf2, &buf4);

    gf_buf_assert_data(
        &buf0, __gf_buf_roundup(sizeof(data0)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data0)), sizeof(data0), data0, sizeof(data0));
    gf_buf_assert_data(
        &buf1, __gf_buf_roundup(sizeof(data3)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data3)), sizeof(data3), data3, sizeof(data3));
    gf_buf_assert_data(
        &buf2, __gf_buf_roundup(sizeof(data4)) > GF_BUF_STACK_THRESHOLD,
        __gf_buf_roundup(sizeof(data4)), sizeof(data4), data4, sizeof(data4));

    assert(gf_buf_equal(&buf1, &buf3));
    assert(gf_buf_equal(&buf2, &buf4));
}

void
gf_buf_test_data_copyout()
{
    char data0[16], data1[64], *d0, *d1, *null;

    memset(data0, 0xa, sizeof(data0));
    memset(data1, 0xb, sizeof(data1));

    gf_buf_decl_init_data(buf0, data0, sizeof(data0));
    gf_buf_decl_init_data(buf1, data1, sizeof(data1));
    gf_buf_decl_null(buf2);

    d0 = gf_buf_copyout(&buf0);
    assert(!memcmp(d0, data0, sizeof(data0)));
    GF_FREE(d0);

    d1 = gf_buf_copyout(&buf1);
    assert(!memcmp(d1, data1, sizeof(data1)));
    GF_FREE(d1);

    null = gf_buf_copyout(&buf2);
    assert(null == NULL);
}

void
gf_buf_test_misc(void)
{
    size_t size;
    char data[16];

    memset(data, '!', sizeof(data));

    gf_buf_decl_null(buf0);
    gf_buf_decl_null(buf1);

    gf_buf_assign_data(&buf0, data, sizeof(data));
    gf_buf_assign_string(&buf1, "test");

    /* data + string -> string */
    gf_buf_concat(&buf0, &buf1);
    gf_buf_assert_simple(&buf0, "!!!!!!!!!!!!!!!!test");

    gf_buf_reset(&buf0);
    gf_buf_reset(&buf1);

    gf_buf_assign_string(&buf0, "test");
    gf_buf_assign_data(&buf1, data, sizeof(data));

    /* string + data -> data */
    gf_buf_concat(&buf0, &buf1);
    size = strlen("test") + sizeof(data);
    gf_buf_assert_data(&buf0, __gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD,
                       __gf_buf_roundup(size), size, "test!!!!!!!!!!!!!!!!",
                       size);

    gf_buf_reset(&buf0);
    gf_buf_reset(&buf1);

    gf_buf_assign_data(&buf0, data, sizeof(data));
    gf_buf_concat_string(&buf0, "test");

    /* data + C string -> C string */
    gf_buf_assert_simple(&buf0, "!!!!!!!!!!!!!!!!test");
}

int
main(int argc, char *argv[])
{
    gf_buf_test_string_basic();
    gf_buf_test_string_compact();
    gf_buf_test_string_copy();
    gf_buf_test_string_equal();
    gf_buf_test_string_concat();
    gf_buf_test_string_null();
    gf_buf_test_string_copyout();

    gf_buf_test_sprintf();
    gf_buf_test_snprintf();

    gf_buf_test_data_basic();
    gf_buf_test_data_compact();
    gf_buf_test_data_copy();
    gf_buf_test_data_equal();
    gf_buf_test_data_concat();
    gf_buf_test_data_null();
    gf_buf_test_data_copyout();

    gf_buf_test_misc();
    return 0;
}
