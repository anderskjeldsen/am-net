#pragma once
#include <libc/core.h>
#include <Am/Net/Socket.h>
#include <Am/Lang/Object.h>
#include <Am/Net/AddressFamily.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

struct _socket_holder {
	int socket;
};

typedef struct _socket_holder socket_holder;
