#include <libc/core.h>
#include <Am/Net/Socket.h>
#include <libc/Am/Net/Socket.h>
#include <Am/Lang/Object.h>
#include <Am/Lang/String.h>
#include <Am/Net/AddressFamily.h>
#include <libc/core_inline_functions.h>

#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>

// --------- Open-fd registry ------------------------------------------------
//
// Tracks every socket fd that has been opened via this AmLang `Socket`
// class and not yet closed. The shutdown hook (`#runOnExit` ->
// `closeAllOpenFds`) iterates this list and `shutdown()` + `close()`
// each entry so any worker thread blocked in `recv()`/`send()`/`connect()`
// wakes with `EBADF`/`ECONNRESET`. The thrown exception propagates
// through the suspend chain (codegen fix in FunctionCallRenderer that
// re-throws child suspend exceptions at the resume label) and the
// worker unwinds out of `TaskRunner.run()` before the runtime sweep.
//
// Why a C-side list and not an AmLang `List<Socket>`:
//   - An AmLang strong-ref list would bump `property_reference_count`
//     and keep every Socket aobject alive until shutdown, defeating the
//     `__native_release_0` backstop and leaking the aobjects between
//     close and shutdown.
//   - A weak-ref list would work but `Weak<T>` has no `.aml` shim in
//     the V1 tree (only native plumbing), so we'd be reviving a
//     half-finished feature.
//   - Tracking fds in C is the cleanest: no ARC interaction, no cycle,
//     and the per-platform `Socket.c` already owns the rest of the
//     fd lifecycle (`socket()`, `close()`).
//
// Sizing: 1024 simultaneous open sockets is generous for AmLang
// programs — IDE chat clients hold 1, web servers maybe a few dozen.
// If a workload outgrows this we'd switch to a dynamic array; for now
// the static cap keeps the code allocation-free.

#define AM_NET_MAX_OPEN_FDS 1024

static pthread_mutex_t s_open_fds_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_open_fds[AM_NET_MAX_OPEN_FDS];
static int s_open_fds_count = 0;

static void am_net_register_fd(int fd) {
    if (fd < 0) return;
    pthread_mutex_lock(&s_open_fds_lock);
    if (s_open_fds_count < AM_NET_MAX_OPEN_FDS) {
        s_open_fds[s_open_fds_count++] = fd;
    }
    // Over-cap: drop the registration silently. Shutdown still closes
    // every other fd we know about; the leaked one is reclaimed by the
    // kernel at process exit. Bumping the cap is cheaper than a dynamic
    // resize and we'd rather not allocate from inside socket()/accept().
    pthread_mutex_unlock(&s_open_fds_lock);
}

static void am_net_unregister_fd(int fd) {
    if (fd < 0) return;
    pthread_mutex_lock(&s_open_fds_lock);
    for (int i = 0; i < s_open_fds_count; i++) {
        if (s_open_fds[i] == fd) {
            // Compact by swapping last element into the freed slot —
            // O(1) and the order doesn't matter since shutdown closes
            // all of them.
            s_open_fds[i] = s_open_fds[--s_open_fds_count];
            break;
        }
    }
    pthread_mutex_unlock(&s_open_fds_lock);
}

// No-op on libc backends — bsdsocket per-task bookkeeping is amigaos /
// morphos-ppc only. The AmLang lambda that targets this symbol is built
// for every platform (Socket.nativeInit() exists everywhere), but only
// the amigaos / morphos-ppc native createSocket paths actually call
// Socket.nativeInit() to register the finalizer that fires this.
function_result Am_Net_Socket_closeBsdsocketForThread_0()
{
	function_result __result = { .has_return_value = false };
	return __result;
}

// Compiler-injected via `#runOnExit` on the AmLang side. Closes every
// fd we registered in `createSocket` / `acceptNative` and never saw
// `close()`'d. The `shutdown(..., SHUT_RDWR)` first is what wakes a
// thread parked in `recv()`/`send()` — `close()` alone is permitted
// to be deferred by the kernel until the in-flight syscall completes
// on some platforms, but shutdown forces the half-close immediately.
function_result Am_Net_Socket_closeAllOpenFds_0(void)
{
    function_result __result = { .has_return_value = false };
    pthread_mutex_lock(&s_open_fds_lock);
    int count = s_open_fds_count;
    int fds[AM_NET_MAX_OPEN_FDS];
    for (int i = 0; i < count; i++) {
        fds[i] = s_open_fds[i];
    }
    s_open_fds_count = 0;
    pthread_mutex_unlock(&s_open_fds_lock);
    // shutdown + close OUTSIDE the lock — close() can block briefly on
    // a TCP linger, and we don't want a slow close to stall a
    // concurrent register/unregister from another worker that hasn't
    // noticed shutdown yet.
    for (int i = 0; i < count; i++) {
        shutdown(fds[i], SHUT_RDWR);
        close(fds[i]);
    }
    return __result;
}

// libc no-op for the amigaos `#runOnStartup` SocketBase capture
// hook. The compiler injects a call to this from generated
// startup.c on every platform; on libc backends there's no
// SocketBase to stash, so the body is empty.
function_result Am_Net_Socket_captureMainSocketBase_0(void)
{
	function_result __result = { .has_return_value = false };
	return __result;
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
};

function_result Am_Net_Socket__native_release_0(aobject * const this)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	// Backstop for users who let a Socket go out of scope without
	// calling `close()`. ARC sweeps the aobject; we close the OS fd
	// and remove it from the shutdown registry so it isn't double-
	// closed by `closeAllOpenFds`. A fd of -1 means `close()` already
	// ran, which is the normal path.
	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	if (s >= 0) {
		am_net_unregister_fd(s);
		close(s);
		this->object_properties.class_object_properties.object_data.value.int_value = -1;
	}
__exit: ;
	return __result;
};

function_result Am_Net_Socket__native_mark_children_0(aobject * const this)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
__exit: ;
	return __result;
};

function_result Am_Net_Socket_createSocket_0(aobject * const this, int addressFamily, int socketType, int protocolFamily)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}

	int s = socket(addressFamily, socketType, protocolFamily);
	if ( s < 0 )
	{
		__throw_simple_exception("Unable to create socket", "in Am_Net_Socket_createSocket_0", &__result);
		goto __exit;
	}

	this->object_properties.class_object_properties.object_data.value.int_value = s;
	// Register in the shutdown-closure list so closeAllOpenFds can
	// wake any blocked recv()/send() on this fd at process exit.
	am_net_register_fd(s);

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	return __result;
};

function_result Am_Net_Socket_connectNative_0(aobject * const this, aobject * hostName, int port, int addressFamily)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}
	if (hostName != NULL) {
		__increase_reference_count(hostName);
	}

	int result = 0;
	long ipadd;
	struct sockaddr_in peer_addr;

	string_holder *host_name_holder = hostName->object_properties.class_object_properties.object_data.value.custom_value;

	struct hostent * hostent = gethostbyname(host_name_holder->string_value);
	if (hostent)
	{
		peer_addr.sin_addr = *(struct in_addr *) hostent->h_addr_list[0];
		peer_addr.sin_family = addressFamily;
		peer_addr.sin_port =  htons(port);

		int s = this->object_properties.class_object_properties.object_data.value.int_value;
		result = connect(s, (struct sockaddr *) &peer_addr, sizeof(struct sockaddr_in));
		if ( result != 0 )
		{
			__throw_simple_exception("Unable to connect to host", "in Am_Net_Socket_connectNative_0", &__result);
			goto __exit;
		}
	}
	else
	{
		__throw_simple_exception("Unable to resolve host", "in Am_Net_Socket_connectNative_0", &__result);
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
};

function_result Am_Net_Socket_send_0(aobject * const this, aobject * bytes, const long long offset, const unsigned int length)
{
	function_result __result = { .has_return_value = true };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}
	if (bytes != NULL) {
		__increase_reference_count(bytes);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;				
	if ( s < 0 )
	{
		__throw_simple_exception("Socket not created", "in Am_Net_Socket_send_0", &__result);
		__returning = true;
		goto __exit;
	}

	array_holder *a_holder = (array_holder *) &bytes[1]; // bytes->object_properties.class_object_properties.object_data.value.custom_value;

	if ((unsigned long long)offset + length > a_holder->size) {
		__throw_simple_exception("Send length is bigger than array", "in Am_Net_Socket_send_0", &__result);
		__returning = true;
		goto __exit;
	}

//	printf("Sending: %s\n", array_holder->array_data);
	int sent = send(s, a_holder->array_data + offset, length, 0);

	if (sent < 0)
	{
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
};

function_result Am_Net_Socket_receive_0(aobject * const this, aobject * bytes, const long long offset, const unsigned int length)
{
	function_result __result = { .has_return_value = true };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}
	if (bytes != NULL) {
		__increase_reference_count(bytes);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;				
	if ( s < 0 )
	{
		__throw_simple_exception("Socket not created", "in Am_Net_Socket_send_0", &__result);
		goto __exit;
	}

	array_holder *a_holder = (array_holder *) &bytes[1]; // bytes->object_properties.class_object_properties.object_data.value.custom_value;

	if ((unsigned long long)offset + length > a_holder->size) {
		__throw_simple_exception("Receive length is bigger than array", "in Am_Net_Socket_send_0", &__result);
		goto __exit;
	}

	int received = recv(s, a_holder->array_data + offset, length, 0);
//	printf("Received %d bytes\n", received);
//	printf("Received data: %s\n", array_holder->array_data);

	if (received < 0)
	{
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
};

function_result Am_Net_Socket_close_0(aobject * const this)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	// Add reference count for this in Socket.close
	if (this != NULL) {
		__increase_reference_count(this);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;				
	if ( s < 0 )
	{
		__throw_simple_exception("Socket not created", "in Am_Net_Socket_send_0", &__result);
		goto __exit;
	}

	am_net_unregister_fd(s);
	close(s);
	this->object_properties.class_object_properties.object_data.value.int_value = -1;

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	return __result;
};

function_result Am_Net_Socket_bindNative_0(aobject * const this, int port, int addressFamily)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}

	struct sockaddr_in server_addr;
	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	
	if (s < 0) {
		__throw_simple_exception("Socket not created", "in Am_Net_Socket_bindNative_0", &__result);
		goto __exit;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = addressFamily;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	// SO_REUSEADDR so a server that was just restarted (or a second
	// app instance re-taking the same port after the first exited)
	// can bind while the old socket lingers in TIME_WAIT, instead of
	// failing with EADDRINUSE.
	int reuse = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const void *) &reuse, sizeof(reuse));

	int result = bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (result < 0) {
		__throw_simple_exception("Unable to bind socket", "in Am_Net_Socket_bindNative_0", &__result);
		goto __exit;
	}

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	return __result;
};

function_result Am_Net_Socket_listenNative_0(aobject * const this, int backlog)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	
	if (s < 0) {
		__throw_simple_exception("Socket not created", "in Am_Net_Socket_listenNative_0", &__result);
		goto __exit;
	}

	printf("Setting socket %d to listen with backlog %d\n", s, backlog);
	
	int result = listen(s, backlog);
	if (result < 0) {
		__throw_simple_exception("Unable to listen on socket", "in Am_Net_Socket_listenNative_0", &__result);
		goto __exit;
	}

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	return __result;
};

function_result Am_Net_Socket_acceptNative_0(aobject * const this, aobject * clientSocket)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}
	if (clientSocket != NULL) {
		__increase_reference_count(clientSocket);
	}

	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	
	if (s < 0) {
		__throw_simple_exception("Socket not created", "in Am_Net_Socket_acceptNative_0", &__result);
		goto __exit;
	}

	printf("Waiting for connection on socket %d\n", s);
	
	int client_socket = accept(s, (struct sockaddr *)&client_addr, &client_len);
	if (client_socket < 0) {
		__throw_simple_exception("Unable to accept connection", "in Am_Net_Socket_acceptNative_0", &__result);
		goto __exit;
	}

	printf("Accepted connection, client socket: %d\n", client_socket);
	clientSocket->object_properties.class_object_properties.object_data.value.int_value = client_socket;
	// Same registry contract as `createSocket` — the accept()'d fd
	// must be tracked so closeAllOpenFds reaches it at shutdown.
	am_net_register_fd(client_socket);

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	if (clientSocket != NULL) {
		__decrease_reference_count(clientSocket);
	}
	return __result;
};



// Detect this machine's own outbound IP via UDP-connect + getsockname.
// See the AmLang doc-comment on Socket.getLocalIpAddress. Returns the
// dotted-quad string, or "" if it can't be determined.
function_result Am_Net_Socket_getLocalIpAddress_0(void)
{
	function_result __result = { .has_return_value = true };
	char ip[64];
	ip[0] = '\0';

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd >= 0) {
		struct sockaddr_in peer;
		memset(&peer, 0, sizeof(peer));
		peer.sin_family = AF_INET;
		peer.sin_port = htons(53);
		// Any routable address — no packet is sent for a UDP connect,
		// it just forces the stack to pick the outgoing interface.
		peer.sin_addr.s_addr = inet_addr("8.8.8.8");
		if (connect(fd, (struct sockaddr *) &peer, sizeof(peer)) == 0) {
			struct sockaddr_in me;
			socklen_t len = sizeof(me);
			memset(&me, 0, sizeof(me));
			if (getsockname(fd, (struct sockaddr *) &me, &len) == 0) {
				const char *p = inet_ntoa(me.sin_addr);
				if (p != NULL) {
					strncpy(ip, p, sizeof(ip) - 1);
					ip[sizeof(ip) - 1] = '\0';
				}
			}
		}
		close(fd);
	}

	__result.return_value.value.object_value = __create_string(ip, &Am_Lang_String);
	return __result;
}

// Enumerate every non-loopback IPv4 address via getifaddrs(). Returns
// them comma-separated. See the AmLang doc-comment on
// Socket.getLocalIpAddresses.
function_result Am_Net_Socket_getLocalIpAddresses_0(void)
{
	function_result __result = { .has_return_value = true };
	char list[512];
	list[0] = '\0';

	struct ifaddrs *ifaddr = NULL;
	if (getifaddrs(&ifaddr) == 0) {
		for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr == NULL) continue;
			if (ifa->ifa_addr->sa_family != AF_INET) continue;
			if ((ifa->ifa_flags & IFF_UP) == 0) continue;
			if (ifa->ifa_flags & IFF_LOOPBACK) continue;
			struct sockaddr_in *sin = (struct sockaddr_in *) ifa->ifa_addr;
			unsigned long hostorder = ntohl(sin->sin_addr.s_addr);
			if ((hostorder >> 24) == 127) continue;   // belt-and-suspenders vs 127.x
			const char *ip = inet_ntoa(sin->sin_addr);
			if (ip == NULL) continue;
			size_t used = strlen(list);
			size_t need = strlen(ip) + (used > 0 ? 1 : 0);
			if (used + need >= sizeof(list)) break;    // out of room
			if (used > 0) { list[used++] = ','; list[used] = '\0'; }
			strcat(list, ip);
		}
		freeifaddrs(ifaddr);
	}

	__result.return_value.value.object_value = __create_string(list, &Am_Lang_String);
	return __result;
}

function_result Am_Net_Socket_setReceiveTimeoutNative_0(aobject * const this, int seconds)
{
	function_result __result = { .has_return_value = false };
	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	if (s >= 0 && seconds > 0) {
		struct timeval tv;
		tv.tv_sec = seconds;
		tv.tv_usec = 0;
		// Best-effort: unsupported stacks just leave the socket blocking.
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const void *) &tv, sizeof(tv));
	}
	return __result;
}
