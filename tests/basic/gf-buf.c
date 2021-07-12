#include <stdio.h>
#include <assert.h>
#include <glusterfs/gf-buf.h>

#define __gf_max(x, y) ((x) > (y) ? (x) : (y))

#define __gf_buf_assert_string(__buf, __data, __allocated, __used)             \
    do {                                                                       \
        assert(!strcmp((__buf)->data, (__data)));                              \
        assert((__buf)->string == 1);                                          \
        assert((__buf)->allocated == __gf_max(GF_BUF_ROUNDUP, (__allocated))); \
        assert((__buf)->used == __used);                                       \
    } while (0)

#define __gf_buf_assert_data(__buf, __data, __size, __allocated, __used)       \
    do {                                                                       \
        assert(!memcmp((__buf)->data, (__data), (__size)));                    \
        assert((__buf)->string == 0);                                          \
        assert((__buf)->allocated == __gf_max(GF_BUF_ROUNDUP, (__allocated))); \
        assert((__buf)->used == __used);                                       \
    } while (0)

void
gf_buf_test_assign_string(void)
{
    char data[] = {0xa, 0xb, 0xc, 0xd};
    char *long0 = "abcdabcdabcdabcdabcdabcdabcdabcd",
         *long1 =
             "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd";

    gf_buf_decl_null(buf0, 16);
    gf_buf_decl_string(buf1, 32);
    gf_buf_decl_string_init(buf2, 32, "test");
    gf_buf_decl_string_init(buf3, 64, "longlonglonglonglonglonglonglong");
    gf_buf_decl_data_init(buf4, 32, data, sizeof(data));

    gf_buf_assign_string(&buf0, "abcdabcd");
    __gf_buf_assert_string(&buf0, "abcdabcd", 16, strlen("abcdabcd") + 1);

    gf_buf_assign_string(&buf1, "x");
    __gf_buf_assert_string(&buf1, "x", 32, strlen("x") + 1);

    gf_buf_assign_string(&buf2, long1);
    __gf_buf_assert_string(&buf2, long1, __gf_buf_roundup(strlen(long1) + 1),
                           strlen(long1) + 1);

    gf_buf_assign_string(&buf3, long0);
    __gf_buf_assert_string(&buf3, long0, 64, strlen(long0) + 1);

    gf_buf_assign_string(&buf4, long1);
    __gf_buf_assert_string(&buf4, long1, __gf_buf_roundup(strlen(long1) + 1),
                           strlen(long1) + 1);
}

void
gf_buf_test_assign_data(void)
{
    char data0[8], data1[16], data2[128], data3[1024], data4[4096];

    memset(data0, 0xa, sizeof(data0));
    memset(data1, 0xb, sizeof(data0));
    memset(data2, 0xc, sizeof(data0));
    memset(data3, 0xd, sizeof(data0));
    memset(data4, 0xe, sizeof(data0));

    gf_buf_decl_null(buf0, 16);
    gf_buf_decl_string(buf1, 32);
    gf_buf_decl_string_init(buf2, 32, "test");
    gf_buf_decl_string_init(buf3, 64, "longlonglonglonglonglonglonglong");
    gf_buf_decl_data_init(buf4, 32, data0, sizeof(data0));

    gf_buf_assign_data(&buf0, data0, sizeof(data0));
    __gf_buf_assert_data(&buf0, data0, sizeof(data0), 16, sizeof(data0));

    gf_buf_assign_data(&buf1, data1, sizeof(data1));
    __gf_buf_assert_data(&buf1, data1, sizeof(data1), 32, sizeof(data1));

    gf_buf_assign_data(&buf2, data2, sizeof(data2));
    __gf_buf_assert_data(&buf2, data2, sizeof(data2), sizeof(data2),
                         sizeof(data2));

    gf_buf_assign_data(&buf3, data3, sizeof(data3));
    __gf_buf_assert_data(&buf3, data3, sizeof(data3), sizeof(data3),
                         sizeof(data3));

    gf_buf_assign_data(&buf4, data4, sizeof(data4));
    __gf_buf_assert_data(&buf4, data4, sizeof(data4), sizeof(data4),
                         sizeof(data4));
}

void
gf_buf_test_copy(void)
{
    char *long0 = "abcdabcdabcdabcdabcdabcdabcdabcd",
         *long1 =
             "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd";
    char data5[8], data6[16], data7[128], data8[1024];

    memset(data5, 0xa, sizeof(data5));
    memset(data6, 0xb, sizeof(data6));
    memset(data7, 0xc, sizeof(data7));
    memset(data8, 0xd, sizeof(data8));

    gf_buf_decl_null(buf0, 8);
    gf_buf_decl_string(buf1, 32);
    gf_buf_decl_string_init(buf2, 16, "abcdabcd");
    gf_buf_decl_string_init(buf3, 64, long0);
    gf_buf_decl_string_init(buf4, 128, long1);

    gf_buf_decl_data_init(buf5, 32, data5, sizeof(data5));
    gf_buf_decl_data_init(buf6, 32, data6, sizeof(data6));
    gf_buf_decl_data_init(buf7, 256, data7, sizeof(data7));
    gf_buf_decl_data_init(buf8, 1024, data8, sizeof(data8));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf1);
    assert(gf_buf_equal(&buf0, &buf1));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf2);
    assert(gf_buf_equal(&buf0, &buf2));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf3);
    assert(gf_buf_equal(&buf0, &buf3));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf4);
    assert(gf_buf_equal(&buf0, &buf4));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf5);
    assert(gf_buf_equal(&buf0, &buf5));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf6);
    assert(gf_buf_equal(&buf0, &buf6));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf7);
    assert(gf_buf_equal(&buf0, &buf7));

    gf_buf_reset(&buf0);
    gf_buf_copy(&buf0, &buf8);
    assert(gf_buf_equal(&buf0, &buf8));

    gf_buf_copy(&buf1, &buf5);
    assert(gf_buf_equal(&buf1, &buf5));
    gf_buf_copy(&buf2, &buf6);
    assert(gf_buf_equal(&buf2, &buf6));

    gf_buf_copy(&buf7, &buf3);
    assert(gf_buf_equal(&buf7, &buf3));
    gf_buf_copy(&buf8, &buf4);
    assert(gf_buf_equal(&buf8, &buf4));
}

void
gf_buf_test_join(void)
{
    char data0[8], data1[16], data2[128], data3[256], data4[512];
    char result0[8 + 16], result1[128 + 256 + 512];

    memset(data0, 0xa, sizeof(data0));
    memset(data1, 0xb, sizeof(data1));
    memset(data2, 0xc, sizeof(data2));
    memset(data3, 0xd, sizeof(data3));
    memset(data4, 0xe, sizeof(data4));

    memset(result0, 0xa, sizeof(data0));
    memset(result0 + sizeof(data0), 0xb, sizeof(data1));

    memset(result1, 0xc, sizeof(data2));
    memset(result1 + sizeof(data2), 0xd, sizeof(data3));
    memset(result1 + sizeof(data2) + sizeof(data3), 0xe, sizeof(data4));

    gf_buf_decl_data_init(buf0, 32, data0, sizeof(data0));
    gf_buf_decl_data_init(buf1, 32, data1, sizeof(data1));
    gf_buf_decl_data_init(buf2, 256, data2, sizeof(data2));
    gf_buf_decl_data_init(buf3, 512, data3, sizeof(data3));
    gf_buf_decl_data_init(buf4, 1024, data4, sizeof(data4));

    assert(gf_buf_join(&buf0, &buf1) == 0);
    __gf_buf_assert_data(&buf0, result0, sizeof(result0), 32, sizeof(result0));

    assert(gf_buf_join(&buf2, &buf3) == 0);
    assert(gf_buf_join(&buf2, &buf4) == 0);
    __gf_buf_assert_data(&buf2, result1, sizeof(result1), sizeof(result1),
                         sizeof(result1));
}

void
gf_buf_test_concat(void)
{
    gf_buf_decl_string(buf0, 16);
    gf_buf_decl_string_init(buf1, 8, "x");
    gf_buf_decl_string_init(buf2, 32, "01234567");
    gf_buf_decl_string_init(buf3, 64, "abcdabcdabcdabcdabcdabcdabcdabcd");

    gf_buf_decl_string(buf4, 32);
    gf_buf_decl_string_init(buf5, 8, "abcd");
    gf_buf_decl_string_init(buf6, 16, "01234567");
    gf_buf_decl_string_init(buf7, 32, "aaaabbbbccccdddd");

    assert(gf_buf_concat(&buf0, &buf1) == 0);
    __gf_buf_assert_string(&buf0, "x", 16, 2);

    assert(gf_buf_concat(&buf2, &buf3) == 0);
    __gf_buf_assert_string(
        &buf2, "01234567abcdabcdabcdabcdabcdabcdabcdabcd",
        __gf_buf_roundup(strlen("01234567abcdabcdabcdabcdabcdabcdabcdabcd") +
                         1),
        strlen("01234567abcdabcdabcdabcdabcdabcdabcdabcd") + 1);

    assert(gf_buf_concat_string(&buf4, "abcd") == 0);
    __gf_buf_assert_string(&buf4, "abcd", 32, strlen("abcd") + 1);

    assert(gf_buf_concat_string(&buf5, "xyzt") == 0);
    __gf_buf_assert_string(&buf5, "abcdxyzt",
                           __gf_buf_roundup(strlen("abcdxyzt") + 1),
                           strlen("abcdxyzt") + 1);

    assert(gf_buf_concat_string(&buf6, "8") == 0);
    __gf_buf_assert_string(&buf6, "012345678", 16,
                           strlen("012345678") + 1);

    assert(gf_buf_concat_string(&buf7, "abcdabcdabcdabcdabcdabcdabcdabcd") ==
           0);
    __gf_buf_assert_string(
        &buf7, "aaaabbbbccccddddabcdabcdabcdabcdabcdabcdabcdabcd",
        __gf_buf_roundup(
            strlen("aaaabbbbccccddddabcdabcdabcdabcdabcdabcdabcdabcd") + 1),
        strlen("aaaabbbbccccddddabcdabcdabcdabcdabcdabcdabcdabcd") + 1);
}

void
gf_buf_test_sprintf(void)
{
    char *p = "oops";
    int x = 123, y = 0xaaaabbbb;
    char *output = "x = 123 y = 0xaaaabbbb p = oops";

    gf_buf_decl_string(buf0, 8);
    gf_buf_decl_string(buf1, 16);
    gf_buf_decl_string_init(buf2, 16, "test");
    gf_buf_decl_string_init(buf3, 32, "abcd0123");

    gf_buf_sprintf(&buf0, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(&buf0, output, __gf_buf_roundup(strlen(output) + 1),
                           strlen(output) + 1);

    gf_buf_sprintf(&buf1, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(&buf1, output, __gf_buf_roundup(strlen(output) + 1),
                           strlen(output) + 1);

    gf_buf_sprintf(&buf2, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(&buf2, output, __gf_buf_roundup(strlen(output) + 1),
                           strlen(output) + 1);

    gf_buf_reset(&buf3);
    gf_buf_sprintf(&buf3, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(&buf2, output, __gf_buf_roundup(strlen(output) + 1),
                           strlen(output) + 1);

    assert(gf_buf_equal(&buf0, &buf1));
    assert(gf_buf_equal(&buf1, &buf2));
    assert(gf_buf_equal(&buf2, &buf3));
}

void
gf_buf_test_snprintf(void)
{
    char *p = "oops";
    int x = 123, y = 0xaaaabbbb;
    char *output = "x = 123 y = 0xaaaabbbb p = oops";

    gf_buf_decl_string(buf0, 8);
    gf_buf_decl_string_init(buf1, 16, "oops");
    gf_buf_decl_string_init(buf2, 64, "longlonglonglonglonglonglonglonglong");

    gf_buf_snprintf(&buf0, 8, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(&buf0, "x = 123", 8, strlen("x = 123") + 1);

    gf_buf_snprintf(&buf1, 23, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(
        &buf1, "x = 123 y = 0xaaaabbbb",
        __gf_buf_roundup(strlen("x = 123 y = 0xaaaabbbb") + 1),
        strlen("x = 123 y = 0xaaaabbbb") + 1);

    gf_buf_snprintf(&buf2, 23, "x = %d y = 0x%x p = %s", x, y, p);
    __gf_buf_assert_string(&buf2, "x = 123 y = 0xaaaabbbb", 64,
                           strlen("x = 123 y = 0xaaaabbbb") + 1);

    assert(gf_buf_equal(&buf1, &buf2));

    gf_buf_snprintf(&buf1, 19, "x = %d y = 0x%x p = %s", x, y, p);
    gf_buf_snprintf(&buf2, 19, "x = %d y = 0x%x p = %s", x, y, p);
    assert(gf_buf_equal(&buf1, &buf2));
}

char *
__make_string(int c, size_t size)
{
    char *ptr = __gf_buf_malloc(size + 1);
    memset(ptr, c, size);
    ptr[size] = '\0';
    return ptr;
}

void
gf_buf_test_misc(void)
{
    int i, nr = 0;
    char *ptr;

    gf_buf_decl_string(buf0, 512);
    gf_buf_decl_string(buf1, 32);
    gf_buf_decl_string(buf2, 512);

    for (i = 0; i < 16; i++) {
	ptr = __make_string('a' + i, (i + 1) * 2);
	gf_buf_assign_string(&buf1, ptr);
	gf_buf_concat(&buf0, &buf1);
	gf_buf_concat_string(&buf2, ptr);
	nr += (i + 1) * 2;
	__gf_buf_free(ptr);
    }

    assert(gf_buf_data_size(&buf0) == nr + 1);
    assert(gf_buf_data_size(&buf2) == nr + 1);
    assert(gf_buf_equal(&buf0, &buf2));

    gf_buf_assign_string(&buf0, "abcd");
    gf_buf_assign_string(&buf2, "0123456789");

    gf_buf_compact(&buf0);
    gf_buf_compact(&buf2);
}

int
main(int argc, char *argv[])
{
    gf_buf_test_assign_string();
    gf_buf_test_assign_data();
    gf_buf_test_copy();
    gf_buf_test_join();
    gf_buf_test_concat();
    gf_buf_test_sprintf();
    gf_buf_test_snprintf();
    gf_buf_test_misc();

    return 0;
}
