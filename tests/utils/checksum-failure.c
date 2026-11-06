#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <glusterfs/checksum.h>
#include <string.h>

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
static int
checksum_fails_without_writing(const unsigned char *data, size_t len)
{
    unsigned char checksum[EVP_MAX_MD_SIZE];
    unsigned char expected[EVP_MAX_MD_SIZE];
    int ret = 0;

    memset(checksum, 0xa5, sizeof(checksum));
    memset(expected, 0xa5, sizeof(expected));
    ret = gf_rsync_md5_checksum((unsigned char *)data, len, checksum);

    return ret != 0 && memcmp(checksum, expected, sizeof(checksum)) == 0;
}
#endif

int
main(void)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    static const unsigned char source[] = "source data";
    static const unsigned char sink[] = "different sink data";

    if (EVP_set_default_properties(NULL, "provider=unavailable") != 1)
        return 1;

    if (!checksum_fails_without_writing(source, sizeof(source)) ||
        !checksum_fails_without_writing(sink, sizeof(sink)))
        return 1;

    if (EVP_set_default_properties(NULL, "") != 1)
        return 1;
#endif

    return 0;
}
