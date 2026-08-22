#include <libc/core.h>
#include <Am/Net/Socket.h>
#include <morphos-ppc/Am/Net/Socket.h>
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

// MorphOS PPC Socket implementation. Ported from native-c/amigaos/Am/Net/Socket.c.
//
// Why per-task bsdsocket bring-up matters on MorphOS too:
//   bsdsocket.library keeps PER-TASK state — an errno-pointer (set via
//   SetErrnoPtr), the per-task socket list, signal masks — keyed by
//   FindTask(NULL). Sharing a SocketBase pointer across tasks does NOT
//   share that state. An AmLang worker (`task-runner-1`) that calls
//   socket() / connect() without its own OpenLibrary + SetErrnoPtr will
//   write through whatever errno pointer was installed by the OPENING
//   task and walk uninitialised per-task slots, crashing in 68k
//   bsdsocket code (typically as a LineF in trance.library because
//   the corrupted slot is read as a function pointer).
//
//   This mirrors the AmiSSL/m68k bug the amigaos port already handles.
//   Same Exec API on MorphOS, same fix: a per-task tc_Launch handler
//   swaps the global `SocketBase` to the task's own base on every
//   dispatch into the task, and each AmLang worker that touches
//   bsdsocket does its own OpenLibrary + SetErrnoPtr the first time.
//
// SocketBase is WEAK so that if a future am-ssl-style helper provides
// a strong definition, both libraries share the same global pointer.
// When am-net is used standalone, this is the sole owner.

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <exec/tasks.h>
#include <exec/execbase.h>

__attribute__((weak)) struct Library *SocketBase = NULL;

// Per-AmLang-Thread tracker of "this task has called OpenLibrary on
// bsdsocket.library and we owe it one CloseLibrary at task exit". See
// the amigaos sibling file for the full rationale — the contract is
// identical on MorphOS. Keyed by the AmLang Thread aobject pointer
// rather than struct Task * so the entry survives the cleanup ordering
// (the finalizer fires from inside the task that opened, but registers
// under the same Thread identity nativeInit used).
struct bsd_task_node {
    aobject *thread;
    struct Library *socket_base;
    struct Task *task;
    int launch_installed;
    struct bsd_task_node *next;
};

// Captured at #runOnStartup from the main task — see
// Am_Net_Socket_captureMainSocketBase_0 below. tc_Launch falls back to
// this for any task that doesn't have its own bsd_task_list entry.
static struct Library *main_socket_base = NULL;

static struct bsd_task_node *bsd_task_list = NULL;

static int bsd_task_is_registered(aobject *thread)
{
    struct bsd_task_node *n;
    int found = 0;
    Forbid();
    for (n = bsd_task_list; n != NULL; n = n->next) {
        if (n->thread == thread) {
            found = 1;
            break;
        }
    }
    Permit();
    return found;
}

static void bsd_task_register(aobject *thread, struct Library *socket_base, struct Task *task)
{
    struct bsd_task_node *node = (struct bsd_task_node *) malloc(sizeof(struct bsd_task_node));
    if (node == NULL) {
        return;
    }
    node->thread = thread;
    node->socket_base = socket_base;
    node->task = task;
    node->launch_installed = 0;
    Forbid();
    node->next = bsd_task_list;
    bsd_task_list = node;
    Permit();
}

static struct bsd_task_node *bsd_task_lookup_node(aobject *thread)
{
    struct bsd_task_node *n;
    struct bsd_task_node *found = NULL;
    Forbid();
    for (n = bsd_task_list; n != NULL; n = n->next) {
        if (n->thread == thread) {
            found = n;
            break;
        }
    }
    Permit();
    return found;
}

// tc_Launch handler — Exec invokes this every time the task gets the
// CPU. Walk bsd_task_list to find the entry whose `task` matches and
// swap the global SocketBase to that entry's per-task base. Falls
// back to main_socket_base so the main task (which has no entry)
// still has a valid global on dispatch.
static void bsdsocket_launch(void)
{
    struct Task *me = SysBase->ThisTask;
    if (me == NULL) {
        return;
    }
    struct bsd_task_node *n;
    for (n = bsd_task_list; n != NULL; n = n->next) {
        if (n->task == me) {
            SocketBase = n->socket_base;
            return;
        }
    }
    if (main_socket_base != NULL) {
        SocketBase = main_socket_base;
    }
}

static void install_launch_handler(struct bsd_task_node *node)
{
    if (node == NULL || node->launch_installed) {
        return;
    }
    Forbid();
    if (node->task != NULL) {
        node->task->tc_Launch = (APTR) bsdsocket_launch;
        node->task->tc_Flags |= TF_LAUNCH;
        node->launch_installed = 1;
    }
    Permit();
}

static int bsd_task_unregister(aobject *thread)
{
    struct bsd_task_node *prev = NULL;
    struct bsd_task_node *n;
    int removed = 0;
    Forbid();
    for (n = bsd_task_list; n != NULL; n = n->next) {
        if (n->thread == thread) {
            if (prev == NULL) {
                bsd_task_list = n->next;
            } else {
                prev->next = n->next;
            }
            removed = 1;
            break;
        }
        prev = n;
    }
    Permit();
    if (removed) {
        free(n);
    }
    return removed;
}

// Look up the AmLang Thread the current task is running on. Set by
// Am.Threading.Thread's _InitTask via tc_UserData on MorphOS too
// (see native-c/morphos-ppc/Am/Threading/Thread.c). Returns NULL on
// the main task or any task not started via AmLang Thread.
static aobject *current_amlang_thread(void)
{
    struct Task *task = FindTask(NULL);
    if (task == NULL) {
        return NULL;
    }
    return (aobject *) task->tc_UserData;
}

// Forward decl — AmLang side registers the finalizer.
extern function_result Am_Net_Socket_f_nativeInit_0(void);

static int ensure_socket_base(void)
{
    aobject *thread;
    struct Library *lib;
    struct Task *task;

    // Legacy path: the global SocketBase isn't set yet (no main-task
    // opener has run). Open from this task to bootstrap.
    if (SocketBase == NULL) {
        SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
        if (SocketBase == NULL) {
            return 0;
        }
        // bsdsocket per-task contract: tell the library where THIS
        // task's errno lives. libnix's errno is already per-task on
        // -noixemul builds, so &errno on the worker resolves to the
        // worker's slot.
        SetErrnoPtr(&errno, sizeof(errno));
        thread = current_amlang_thread();
        if (thread != NULL && !bsd_task_is_registered(thread)) {
            task = FindTask(NULL);
            bsd_task_register(thread, SocketBase, task);
            install_launch_handler(bsd_task_lookup_node(thread));
            Am_Net_Socket_f_nativeInit_0();
        }
        return 1;
    }

    // Hot path: SocketBase is already set (main task opened it at
    // #runOnStartup time). For the main task there's nothing more to
    // do. For an AmLang worker, do THIS task's own OpenLibrary +
    // SetErrnoPtr so bsdsocket initialises the worker's per-task slot.
    // The returned base MAY differ from the main task's under
    // Roadshow/Miami-style stacks; capture it, install tc_Launch so
    // each dispatch into this worker swaps the global to its own
    // base, then store the global now (we're on the worker).
    thread = current_amlang_thread();
    if (thread == NULL) {
        // Main task — already wired by captureMainSocketBase. Nothing
        // to do.
        return 1;
    }
    if (bsd_task_is_registered(thread)) {
        return 1;
    }
    lib = OpenLibrary((STRPTR)"bsdsocket.library", 4);
    if (lib == NULL) {
        return 0;
    }
    task = FindTask(NULL);
    bsd_task_register(thread, lib, task);
    install_launch_handler(bsd_task_lookup_node(thread));
    SocketBase = lib;
    SetErrnoPtr(&errno, sizeof(errno));
    // Hand control back to AmLang so the Thread.addFinalizer call is
    // a real lambda registration through the normal codegen path.
    Am_Net_Socket_f_nativeInit_0();
    return 1;
}

// --------- Open-fd registry ------------------------------------------------
//
// Mirrors the libc Socket.c registry but uses `Forbid()`/`Permit()` and
// `CloseSocket()`. See the libc file for the rationale on why this
// lives in C instead of an AmLang-side `List<Socket>`.
#define AM_NET_MAX_OPEN_FDS 256

static int s_open_fds[AM_NET_MAX_OPEN_FDS];
static int s_open_fds_count = 0;

static void am_net_register_fd(int fd) {
    if (fd < 0) return;
    Forbid();
    if (s_open_fds_count < AM_NET_MAX_OPEN_FDS) {
        s_open_fds[s_open_fds_count++] = fd;
    }
    Permit();
}

static void am_net_unregister_fd(int fd) {
    if (fd < 0) return;
    Forbid();
    for (int i = 0; i < s_open_fds_count; i++) {
        if (s_open_fds[i] == fd) {
            s_open_fds[i] = s_open_fds[--s_open_fds_count];
            break;
        }
    }
    Permit();
}

function_result Am_Net_Socket_closeAllOpenFds_0(void)
{
    function_result __result = { .has_return_value = false };
    int count;
    int fds[AM_NET_MAX_OPEN_FDS];
    Forbid();
    count = s_open_fds_count;
    for (int i = 0; i < count; i++) {
        fds[i] = s_open_fds[i];
    }
    s_open_fds_count = 0;
    Permit();
    if (SocketBase != NULL) {
        for (int i = 0; i < count; i++) {
            CloseSocket(fds[i]);
        }
    }
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
}

function_result Am_Net_Socket__native_release_0(aobject * const this)
{
    function_result __result = { .has_return_value = false };
    bool __returning = false;
    // Backstop for users who let a Socket go out of scope without
    // calling `close()`. ARC sweeps the aobject; we CloseSocket() the
    // OS fd and remove it from the shutdown registry.
    int s = this->object_properties.class_object_properties.object_data.value.int_value;
    if (s >= 0 && SocketBase != NULL) {
        am_net_unregister_fd(s);
        CloseSocket(s);
        this->object_properties.class_object_properties.object_data.value.int_value = -1;
    }
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
    // Register in the shutdown-closure list so closeAllOpenFds can
    // wake any blocked recv()/send() on this fd at process exit.
    am_net_register_fd(s);

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
    am_net_unregister_fd(s);
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

    // SO_REUSEADDR so a restart / second instance can re-bind the
    // port while the old socket lingers in TIME_WAIT (see libc file).
    {
        int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse));
    }

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
    ULONG client_len = sizeof(client_addr);
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
    am_net_register_fd(client_socket);

__exit: ;
    if (this != NULL) {
        __decrease_reference_count(this);
    }
    if (clientSocket != NULL) {
        __decrease_reference_count(clientSocket);
    }
    return __result;
}

// #runOnStartup hook — invoked from generated startup.c before
// user main() runs. Two things:
//   1. Stash whatever SocketBase value the main task's startup left
//      behind (if any). tc_Launch uses this as the fallback for
//      tasks that don't have their own bsd_task_list entry.
//   2. Install tc_Launch on the main task so every dispatch INTO
//      main restores `SocketBase = main_socket_base` — race-free
//      against worker writes to the global.
//
// On MorphOS we don't have an amisslauto-style constructor that
// pre-opens bsdsocket; SocketBase may still be NULL at this point.
// That's fine — `ensure_socket_base` on first use from any task
// handles the lazy open, and once any task has opened it the
// global is non-NULL for the rest of process lifetime.
function_result Am_Net_Socket_captureMainSocketBase_0(void)
{
    function_result __result = { .has_return_value = false };
    main_socket_base = SocketBase;
    struct Task *main_task = FindTask(NULL);
    if (main_task != NULL) {
        Forbid();
        main_task->tc_Launch = (APTR) bsdsocket_launch;
        main_task->tc_Flags |= TF_LAUNCH;
        Permit();
    }
    return __result;
}

// Counterpart to the per-task `OpenLibrary` in `ensure_socket_base`.
// Reached via the Thread finalizer that nativeInit() registered, so
// the call site is on the same task that opened. We:
//   1. Clear tc_Launch + TF_LAUNCH on the worker (the node is about
//      to be freed and the task itself is exiting).
//   2. CloseLibrary on the PER-TASK base captured in the node — not
//      the global SocketBase, which the main task's tc_Launch handler
//      restores on its next dispatch.
function_result Am_Net_Socket_closeBsdsocketForThread_0(void)
{
    function_result __result = { .has_return_value = false };
    aobject *thread = current_amlang_thread();
    if (thread == NULL) {
        return __result;
    }
    struct bsd_task_node *node = bsd_task_lookup_node(thread);
    struct Library *to_close = NULL;
    if (node != NULL) {
        to_close = node->socket_base;
        Forbid();
        if (node->task != NULL && node->launch_installed) {
            node->task->tc_Flags &= ~TF_LAUNCH;
            node->task->tc_Launch = NULL;
            node->launch_installed = 0;
        }
        Permit();
    }
    if (bsd_task_unregister(thread)) {
        if (to_close != NULL) {
            CloseLibrary(to_close);
        }
    }
    return __result;
}

// Detect this machine's own outbound IP via UDP-connect + getsockname,
// against MorphOS bsdsocket.library. See the AmLang doc-comment on
// Socket.getLocalIpAddress. Returns the dotted-quad string, or "".
function_result Am_Net_Socket_getLocalIpAddress_0(void)
{
	function_result __result = { .has_return_value = true };
	char ip[64];
	ip[0] = '\0';

	if (ensure_socket_base()) {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd >= 0) {
			struct sockaddr_in peer;
			memset(&peer, 0, sizeof(peer));
			peer.sin_family = AF_INET;
			peer.sin_port   = htons(53);
			peer.sin_len    = sizeof(struct in_addr);
			peer.sin_addr.s_addr = inet_addr((STRPTR) "8.8.8.8");
			if (connect(fd, (struct sockaddr *) &peer, sizeof(peer)) == 0) {
				struct sockaddr_in me;
				LONG len = sizeof(me);
				memset(&me, 0, sizeof(me));
				if (getsockname(fd, (struct sockaddr *) &me, &len) == 0) {
					char *p = (char *) Inet_NtoA(me.sin_addr.s_addr);
					if (p != NULL) {
						strncpy(ip, p, sizeof(ip) - 1);
						ip[sizeof(ip) - 1] = '\0';
					}
				}
			}
			CloseSocket(fd);
		}
	}

	__result.return_value.value.object_value = __create_string(ip, &Am_Lang_String);
	return __result;
}
// Enumerate every non-loopback IPv4 address via SIOCGIFCONF against
// bsdsocket. Falls back to the single default-route address
// (UDP-connect + getsockname) when the stack's interface walk yields
// nothing. Returns them comma-separated. See the AmLang doc-comment
// on Socket.getLocalIpAddresses.
function_result Am_Net_Socket_getLocalIpAddresses_0(void)
{
	function_result __result = { .has_return_value = true };
	char list[512];
	list[0] = '\0';

	if (ensure_socket_base()) {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd >= 0) {
			static char ifbuf[4096];
			struct ifconf ifc;
			memset(&ifc, 0, sizeof(ifc));
			ifc.ifc_len = sizeof(ifbuf);
			ifc.ifc_buf = ifbuf;
			if (IoctlSocket(fd, SIOCGIFCONF, (char *) &ifc) == 0) {
				char *ptr  = ifbuf;
				char *stop = ifbuf + ifc.ifc_len;
				while (ptr < stop) {
					struct ifreq *ifr = (struct ifreq *) ptr;
					// BSD 4.4 sockaddr carries sa_len; entries are
					// variable-length. Clamp to a full struct sockaddr
					// so a zero sa_len can't stall the walk.
					int salen = ifr->ifr_addr.sa_len;
					if (salen < (int) sizeof(struct sockaddr)) {
						salen = sizeof(struct sockaddr);
					}
					if (ifr->ifr_addr.sa_family == AF_INET) {
						struct sockaddr_in *sin = (struct sockaddr_in *) &ifr->ifr_addr;
						unsigned long ho = ntohl(sin->sin_addr.s_addr);
						if ((ho >> 24) != 127 && sin->sin_addr.s_addr != 0) {
							char *ip = (char *) Inet_NtoA(sin->sin_addr.s_addr);
							if (ip != NULL) {
								size_t used = strlen(list);
								size_t need = strlen(ip) + (used > 0 ? 1 : 0);
								if (used + need < sizeof(list)) {
									if (used > 0) { list[used++] = ','; list[used] = '\0'; }
									strcat(list, ip);
								}
							}
						}
					}
					ptr += sizeof(ifr->ifr_name) + salen;
				}
			}
			CloseSocket(fd);
		}
	}

	if (list[0] == '\0') {
		// Fallback: single default-route address.
		if (ensure_socket_base()) {
			int fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (fd >= 0) {
				struct sockaddr_in peer;
				memset(&peer, 0, sizeof(peer));
				peer.sin_family = AF_INET;
				peer.sin_port   = htons(53);
				peer.sin_len    = sizeof(struct in_addr);
				peer.sin_addr.s_addr = inet_addr((STRPTR) "8.8.8.8");
				if (connect(fd, (struct sockaddr *) &peer, sizeof(peer)) == 0) {
					struct sockaddr_in me;
					LONG len = sizeof(me);
					memset(&me, 0, sizeof(me));
					if (getsockname(fd, (struct sockaddr *) &me, &len) == 0) {
						char *ip = (char *) Inet_NtoA(me.sin_addr.s_addr);
						if (ip != NULL) {
							strncpy(list, ip, sizeof(list) - 1);
							list[sizeof(list) - 1] = '\0';
						}
					}
				}
				CloseSocket(fd);
			}
		}
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
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(tv));
	}
	return __result;
}
