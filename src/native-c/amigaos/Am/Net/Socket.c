#include <libc/core.h>
#include <Am/Net/Socket.h>
#include <amigaos/Am/Net/Socket.h>
#include <Am/Lang/ClassRef.h>
#include <Am/Lang/Object.h>
#include <Am/Net/AddressFamily.h>
#include <Am/Net/SocketType.h>
#include <Am/Net/ProtocolFamily.h>
#include <Am/Lang/String.h>
#include <Am/Lang/Int.h>
#include <Am/Lang/UInt.h>
#include <Am/Lang/UByte.h>
#include <Am/Lang/Array.h>
#include <libc/core_inline_functions.h>

// AmigaOS m68k Socket implementation. Mirrors src/native-c/libc/Am/Net/Socket.c
// shape-for-shape, but the BSD socket calls dispatch through
// <proto/socket.h> against an explicit `SocketBase`, and `addr.sin_len`
// is set on connect — both required so an `SslSocketStream` wrapping
// this socket can hand the resulting fd to AmiSSL successfully. The
// libnix POSIX route (which the libc Socket.c uses) leaves the fd in a
// state AmiSSL's recv()/send() wrappers don't recognise, leading to a
// silent peer-close during the TLS handshake.
//
// SocketBase ownership:
//   We declare it WEAK here so that when am-ssl is also linked, am-ssl's
//   strong definition (in src/native-c/amigaos/amissl_init.c) wins and
//   both libraries use the *same* SocketBase. When am-net is used
//   standalone (no am-ssl), am-net's weak definition is the only one
//   and we're the sole owner.
//
//   Either way, the first `Socket.create()` lazily opens
//   bsdsocket.library v4 if SocketBase is still NULL — bsdsocket is
//   per-task and idempotent across multiple opens in the same task,
//   so calling OpenLibrary again later from am-ssl is safe.

#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

__attribute__((weak)) struct Library *SocketBase = NULL;

static int ensure_socket_base(void)
{
    if (SocketBase) {
        return 1;
    }
    SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
    return (SocketBase != NULL);
}

function_result Am_Net_Socket__native_init_0(aobject * const this)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    if (this != NULL) {
        __increase_reference_count(this);
    }
__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    return __result;
}

function_result Am_Net_Socket__native_release_0(aobject * const this)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
__exit: ;
    return __result;
}

function_result Am_Net_Socket__native_mark_children_0(aobject * const this)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
__exit: ;
    return __result;
}

function_result Am_Net_Socket_createSocket_0(aobject * const this, int addressFamily, int socketType, int protocolFamily)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    int s;

    if (this != NULL) {
        __increase_reference_count(this);
    }

    if (!ensure_socket_base()) {
        __throw_simple_exception("Couldn't open bsdsocket.library v4", "in Am_Net_Socket_createSocket_0", &__result);
        goto __exit;
    }

    printf("create socket %d, %d, %d\n", addressFamily, socketType, protocolFamily);
    s = socket(addressFamily, socketType, protocolFamily);
    printf("newsocket %d\n", s);
    if (s < 0) {
        __throw_simple_exception("Unable to create socket", "in Am_Net_Socket_createSocket_0", &__result);
        goto __exit;
    }

    this->object_properties.class_object_properties.object_data.value.int_value = s;

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    return __result;
}

function_result Am_Net_Socket_connectNative_0(aobject * const this, aobject * hostName, int port, int addressFamily)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    int result = 0;
    struct sockaddr_in peer_addr;
    string_holder *host_name_holder;
    struct hostent *he;
    int s;

    if (this != NULL) {
        __increase_reference_count(this);
    }
    if (hostName != NULL) {
        __increase_reference_count(hostName);
    }

    host_name_holder = hostName->object_properties.class_object_properties.object_data.value.custom_value;

    printf("host name: %s\n", host_name_holder->string_value);
    he = gethostbyname((STRPTR)host_name_holder->string_value);
    if (!he) {
        __throw_simple_exception("Unable to resolve host", "in Am_Net_Socket_connectNative_0", &__result);
        goto __exit;
    }

    printf("host: %d\n", *(int *)he->h_addr_list[0]);
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_addr   = *(struct in_addr *)he->h_addr_list[0];
    peer_addr.sin_family = addressFamily;
    peer_addr.sin_port   = htons(port);
    peer_addr.sin_len    = he->h_length;  // bsdsocket-on-Amiga expects
                                          // a non-zero sin_len; mirrors
                                          // AmiSSL's test/https.c.

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    printf("socket %d\n", s);
    result = connect(s, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    if (result != 0) {
        __throw_simple_exception("Unable to connect to host", "in Am_Net_Socket_connectNative_0", &__result);
        goto __exit;
    }

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    if (hostName != NULL) {
        __decrease_reference_count(hostName);
    }
    return __result;
}

function_result Am_Net_Socket_send_0(aobject * const this, aobject * bytes, const long long offset, const unsigned int length)
{
    function_result __result = { .has_return_value = true };
    bool __returning = false;
    int s;
    array_holder *a_holder;
    int sent;

    if (this != NULL) {
        __increase_reference_count(this);
    }
    if (bytes != NULL) {
        __increase_reference_count(bytes);
    }

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s < 0) {
        __throw_simple_exception("Socket not created", "in Am_Net_Socket_send_0", &__result);
        __returning = true;
        goto __exit;
    }

    a_holder = (array_holder *)&bytes[1];
    if ((unsigned long long)offset + length > a_holder->size) {
        __throw_simple_exception("Send length is bigger than array", "in Am_Net_Socket_send_0", &__result);
        __returning = true;
        goto __exit;
    }

    sent = send(s, a_holder->array_data + offset, length, 0);
    if (sent < 0) {
        __throw_simple_exception("Error sending data", "in Am_Net_Socket_send_0", &__result);
        goto __exit;
    }

    __result.return_value.value.uint_value = sent;
    __result.return_value.flags = PRIMITIVE_UINT;

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    if (bytes != NULL) {
        __decrease_reference_count(bytes);
    }
    return __result;
}

function_result Am_Net_Socket_receive_0(aobject * const this, aobject * bytes, const long long offset, const unsigned int length)
{
    function_result __result = { .has_return_value = true };
    bool __returning = false;
    int s;
    array_holder *a_holder;
    int received;

    if (this != NULL) {
        __increase_reference_count(this);
    }
    if (bytes != NULL) {
        __increase_reference_count(bytes);
    }

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s < 0) {
        __throw_simple_exception("Socket not created", "in Am_Net_Socket_receive_0", &__result);
        goto __exit;
    }

    a_holder = (array_holder *)&bytes[1];
    if ((unsigned long long)offset + length > a_holder->size) {
        __throw_simple_exception("Receive length is bigger than array", "in Am_Net_Socket_receive_0", &__result);
        goto __exit;
    }

    received = recv(s, a_holder->array_data + offset, length, 0);
    if (received < 0) {
        __throw_simple_exception("Error receiving data", "in Am_Net_Socket_receive_0", &__result);
        goto __exit;
    }

    __result.return_value.value.uint_value = received;
    __result.return_value.flags = PRIMITIVE_UINT;
    __returning = true;

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    if (bytes != NULL) {
        __decrease_reference_count(bytes);
    }
    return __result;
}

function_result Am_Net_Socket_close_0(aobject * const this)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    int s;

    if (this != NULL) {
        __increase_reference_count(this);
    }

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s < 0) {
        __throw_simple_exception("Socket not created", "in Am_Net_Socket_close_0", &__result);
        goto __exit;
    }

    // bsdsocket-on-Amiga sockets MUST be closed with CloseSocket(), not
    // libnix's close(). close() routes through DOS file descriptors and
    // would leak the bsdsocket entry.
    CloseSocket(s);
    this->object_properties.class_object_properties.object_data.value.int_value = -1;

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    return __result;
}

function_result Am_Net_Socket_bindNative_0(aobject * const this, int port, int addressFamily)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    struct sockaddr_in server_addr;
    int s;
    int result;

    if (this != NULL) {
        __increase_reference_count(this);
    }

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s < 0) {
        __throw_simple_exception("Socket not created", "in Am_Net_Socket_bindNative_0", &__result);
        goto __exit;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = addressFamily;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(port);
    server_addr.sin_len         = sizeof(struct in_addr);

    printf("Binding socket %d to port %d\n", s, port);
    result = bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (result < 0) {
        __throw_simple_exception("Unable to bind socket", "in Am_Net_Socket_bindNative_0", &__result);
        goto __exit;
    }

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    return __result;
}

function_result Am_Net_Socket_listenNative_0(aobject * const this, int backlog)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    int s;
    int result;

    if (this != NULL) {
        __increase_reference_count(this);
    }

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s < 0) {
        __throw_simple_exception("Socket not created", "in Am_Net_Socket_listenNative_0", &__result);
        goto __exit;
    }

    printf("Setting socket %d to listen with backlog %d\n", s, backlog);
    result = listen(s, backlog);
    if (result < 0) {
        __throw_simple_exception("Unable to listen on socket", "in Am_Net_Socket_listenNative_0", &__result);
        goto __exit;
    }

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    return __result;
}

function_result Am_Net_Socket_acceptNative_0(aobject * const this, aobject * clientSocket)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int s;
    int client_socket;

    if (this != NULL) {
        __increase_reference_count(this);
    }
    if (clientSocket != NULL) {
        __increase_reference_count(clientSocket);
    }

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s < 0) {
        __throw_simple_exception("Socket not created", "in Am_Net_Socket_acceptNative_0", &__result);
        goto __exit;
    }

    printf("Waiting for connection on socket %d\n", s);
    client_socket = accept(s, (struct sockaddr *)&client_addr, &client_len);
    if (client_socket < 0) {
        __throw_simple_exception("Unable to accept connection", "in Am_Net_Socket_acceptNative_0", &__result);
        goto __exit;
    }

    printf("Accepted connection, client socket: %d\n", client_socket);
    clientSocket->object_properties.class_object_properties.object_data.value.int_value = client_socket;

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    if (clientSocket != NULL) {
        __decrease_reference_count(clientSocket);
    }
    return __result;
}
