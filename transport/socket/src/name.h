#ifndef NAME_H
#define NAME_H

int32_t 
client_bind (transport_t *this, 
	     struct sockaddr *sockaddr, 
	     socklen_t *sockaddr_len, 
	     int sock);

int32_t
client_get_remote_sockaddr (transport_t *this, 
			    struct sockaddr *sockaddr, 
			    socklen_t *sockaddr_len);

int32_t
client_get_remote_sockaddr (transport_t *this, 
			    struct sockaddr *sockaddr, 
			    socklen_t *sockaddr_len);

int32_t
server_get_local_sockaddr (transport_t *this, 
			   struct sockaddr *addr, 
			   socklen_t *addr_len);

int32_t
get_transport_identifiers (transport_t *this);

#endif
