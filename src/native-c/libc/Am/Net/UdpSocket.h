#pragma once
#include <libc/core.h>
#include <Am/Net/UdpSocket.h>
#include <Am/Lang/Object.h>
#include <Am/Net/AddressFamily.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

struct _udp_socket_holder {
    int socket;
    struct sockaddr_in addr;
};

typedef struct _udp_socket_holder udp_socket_holder;