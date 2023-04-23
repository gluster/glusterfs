#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>

int
gf_is_ip_in_net(const char *network, const char *ip_str)
{
    /* A buffer big enough for any socket address. */
    uint8_t net_buff[sizeof(struct sockaddr_storage) + 1];
    uint8_t ip_buff[sizeof(struct sockaddr_storage) + 1];
    int32_t size;
    uint8_t mask;
    int ret = 0;
    int family = AF_INET;

    if (strchr(network, ':'))
        family = AF_INET6;
    else if (strchr(network, '.'))
        family = AF_INET;
    else {
        goto out;
    }

    size = inet_net_pton(family, network, net_buff, sizeof(net_buff));
    if (size < 0) {
        fprintf(stderr, "inet_net_pton: %s\n", strerror(errno));
        goto out;
    }

    ret = inet_pton(family, ip_str, &ip_buff);
    if (ret < 0) {
        fprintf(stderr, "inet_pton: %s\n", strerror(errno));
        goto out;
    }

    mask = (0xff00 >> (size & 7)) & 0xff;
    size /= 8;
    net_buff[size] &= mask;
    ip_buff[size] &= mask;

    return memcmp(net_buff, ip_buff, size + 1) == 0;
out:
    return ret;
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Syntax: ip_in_cidr <subnet_address> <ip_address>");
        return 1;
    }
    char *subnet_str = argv[1];
    char *ip_addr = argv[2];
    int result = 0;

    result = gf_is_ip_in_net(subnet_str, ip_addr);
    if (result) {
        fprintf(stdout, "YES\n");
    } else {
        fprintf(stdout, "NO\n");
    }
    return 0;
}
