#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>

#ifndef strdupa
#define strdupa(s)                                                             \
    (__extension__({                                                           \
        __const char *__old = (s);                                             \
        size_t __len = strlen(__old) + 1;                                      \
        char *__new = (char *)__builtin_alloca(__len);                         \
        (char *)memcpy(__new, __old, __len);                                   \
    }))
#endif

int
gf_is_ip_in_net(const char *network, const char *ip_str)
{
    unsigned long ip_buf = 0;
    unsigned long net_ip_buf = 0;
    unsigned long subnet_mask = 0;
    int ret = -1;
    char *slash = NULL;
    char *net_ip = NULL;
    char *subnet = NULL;
    char *net_str = NULL;
    int family = AF_INET;
    int result = 0;

    if (strchr(network, ':'))
        family = AF_INET6;
    else if (strchr(network, '.'))
        family = AF_INET;
    else {
        goto out;
    }

    net_str = strdupa(network);
    slash = strchr(net_str, '/');
    if (!slash)
        goto out;
    *slash = '\0';

    subnet = slash + 1;
    net_ip = net_str;

    /* Convert IP address to a long */
    ret = inet_pton(family, ip_str, &ip_buf);
    if (ret < 0) {
        fprintf(stderr, "inet_pton: %s\n", strerror(errno));
        goto out;
    }
    /* Convert network IP address to a long */
    ret = inet_pton(family, net_ip, &net_ip_buf);
    if (ret < 0) {
        fprintf(stderr, "inet_pton: %s\n", strerror(errno));
        goto out;
    }

    /* convert host byte order to network byte order */
    ip_buf = (unsigned long)htonl(ip_buf);
    net_ip_buf = (unsigned long)htonl(net_ip_buf);

    /* Converts /x into a mask */
    subnet_mask = (0xffffffff << (32 - atoi(subnet))) & 0xffffffff;

    result = ((ip_buf & subnet_mask) == (net_ip_buf & subnet_mask));
out:
    return result;
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
