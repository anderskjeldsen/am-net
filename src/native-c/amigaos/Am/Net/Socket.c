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
#include <sys/ioctl.h>
#include <net/if.h>

#include <exec/tasks.h>

__attribute__((weak)) struct Library *SocketBase = NULL;

// Per-AmLang-Thread tracker of "this task has called OpenLibrary on
// bsdsocket.library and we owe it one CloseLibrary at task exit".
//
// Why per-task: bsdsocket.library keeps per-task state keyed by
// FindTask(NULL) — errno location, signal handler, socket list. A
// task that uses sockets without having called OpenLibrary itself
// shares the base pointer fine (it's the same struct Library * for
// everyone) but has no per-task slot, which kills SSL handshakes
// even when raw socket() works (amiberry's bsdsocket_emu happens to
// be lenient about this; Miami/RoadShow are not).
//
// We don't write to the global SocketBase from per-task opens — that
// pointer is owned by amisslauto (or by the first-ever opener via
// the fallback below) and is the same value every OpenLibrary call
// returns. Per-task opens only matter for the side effect.
//
// The list is keyed by the AmLang Thread aobject pointer rather than
// struct Task * so it survives the cleanup ordering on amigaos: the
// finalizer runs from inside the task that opened, but uses the same
// Thread identity that nativeInit registered with.
//
// `socket_base` is the per-task bsdsocket base — what THIS task got
// back from its own `OpenLibrary("bsdsocket.library", 4)`. Roadshow
// / Miami / amiberry default semantics: each opener gets its own
// base, NOT a shared one (AmiSSL dev confirmed).
//
// `task` is the AmigaOS Task pointer the bsdsocket open ran on; the
// tc_Launch handler reads `SysBase->ThisTask` and walks this list
// to find the matching entry, then sets the global SocketBase to
// node->socket_base. That way proto/socket.h inlines that
// dereference the global pick up the right per-task base on every
// dispatch into this task. AmiSSL gets the same per-task base via
// `InitAmiSSL(AmiSSL_SocketBase, …)` so SSL_read/SSL_write also
// route through the right slot.
//
// `launch_installed` is a guard so we only wire tc_Launch + TF_LAUNCH
// once per task — re-installs would clobber any other handler that
// might have set them (none today; AmLang owns its worker tasks).
struct bsd_task_node {
    aobject *thread;
    struct Library *socket_base;
    struct Task *task;
    int launch_installed;
    struct bsd_task_node *next;
};

// Stashed at program startup via the #runOnStartup hook in this
// file (Am_Net_Socket_captureMainSocketBase_0 below). amisslauto
// opens bsdsocket on the main task at constructor time and writes
// the global SocketBase; we capture that value before any worker
// has a chance to overwrite the global, so the worker's finalizer
// can restore it before amisslauto's destructor closes it.
static struct Library *main_socket_base = NULL;

static struct bsd_task_node *bsd_task_list = NULL;

// Forbid()/Permit() is the simplest mutual exclusion across AmigaOS
// tasks — used because the same TaskScheduler IO worker could be
// touching the list concurrently with a future second worker.
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

// Look up the per-task node by AmLang Thread identity. Linear scan
// (the list is small — one entry per AmLang worker that touched
// bsdsocket); only called on bring-up and teardown.
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

// Public accessor for am-ssl (and any other consumer) to obtain the
// CURRENT task's bsdsocket base — what they should pass to
// InitAmiSSL via AmiSSL_SocketBase. Returns NULL on the main task or
// a non-AmLang task (caller should fall back to the global
// SocketBase amisslauto set up in those cases).
struct Library *am_net_current_task_socket_base(void)
{
    struct Task *me = FindTask(NULL);
    if (me == NULL) {
        return NULL;
    }
    aobject *thread = (aobject *) me->tc_UserData;
    if (thread == NULL) {
        return NULL;
    }
    struct bsd_task_node *node = bsd_task_lookup_node(thread);
    if (node == NULL) {
        return NULL;
    }
    return node->socket_base;
}

// tc_Launch handler — Exec invokes this every time this task is
// dispatched (gets the CPU). The handler walks bsd_task_list to
// find the entry whose `task` matches the currently-running Task,
// then sets the global SocketBase to that entry's per-task base.
// If no entry matches (e.g. main task — main never opens bsdsocket
// itself, amisslauto does it for it), we fall back to
// main_socket_base which was stashed by the `#runOnStartup` hook.
//
// Installed on BOTH the main task (via captureMainSocketBase) and
// every AmLang worker task that touches bsdsocket (via
// install_launch_handler below). Each dispatch into either context
// puts the right base in the global before any proto/socket.h
// inline runs.
//
// Runs from the dispatcher with the task's own SP — regular
// user-stack semantics, but should be small + fast. Uses the
// global `SysBase` from proto/exec.h.
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
    // No worker entry for this task — main task, or any non-AmLang
    // task that doesn't manage its own per-task base. Use the main
    // task's saved base (captured at #runOnStartup time).
    if (main_socket_base != NULL) {
        SocketBase = main_socket_base;
    }
}

// Wire the tc_Launch handler onto the given task once. Idempotent
// per node. Forbid() so the writes to tc_Launch and tc_Flags are
// visible to the dispatcher in one go.
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

// #runOnStartup hook — invoked from generated startup.c BEFORE
// user main() runs, AFTER class init / static-property setup, so
// `amisslauto`'s constructor has already opened bsdsocket on the
// main task and written the global SocketBase.
//
// Two things we do here:
//   1. Stash the main task's bsdsocket base. tc_Launch uses it as
//      the fallback for any task that doesn't have its own entry
//      in bsd_task_list (i.e. main itself, and any non-AmLang task).
//   2. Install tc_Launch on the main task. Every dispatch INTO
//      main then sets `SocketBase = main_socket_base` — race-free,
//      no matter what value a worker may have written to the
//      global while running. (Trying to restore in #runOnExit
//      instead would race with workers still running; tc_Launch
//      handles it at the dispatcher level.)
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

// Look up the AmLang Thread the current task is running on (set by
// Am.Threading.Thread's _InitTask via tc_UserData). Returns NULL on
// the main task or any task that wasn't started via AmLang Thread.
static aobject *current_amlang_thread(void)
{
    struct Task *task = FindTask(NULL);
    if (task == NULL) {
        return NULL;
    }
    return (aobject *) task->tc_UserData;
}

static int ensure_socket_base(void)
{
    aobject *thread;
    struct Library *lib;
    struct Task *task;

    // Legacy path: the global SocketBase isn't set yet (no
    // amisslauto, never opened by main). Open from this task to
    // bootstrap the pointer for everyone.
    if (SocketBase == NULL) {
        SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
        if (SocketBase == NULL) {
            return 0;
        }
        // If this happened on an AmLang Thread, register it so the
        // matching CloseLibrary runs at task exit + install the
        // launch handler so subsequent dispatches restore this base.
        thread = current_amlang_thread();
        if (thread != NULL && !bsd_task_is_registered(thread)) {
            task = FindTask(NULL);
            bsd_task_register(thread, SocketBase, task);
            install_launch_handler(bsd_task_lookup_node(thread));
            Am_Net_Socket_f_nativeInit_0();
        }
        return 1;
    }

    // Hot path: SocketBase is already set (amisslauto opened it on
    // main task at constructor time). For the main task itself
    // there's nothing more to do — keep using the global. For an
    // AmLang worker thread, do THIS thread's own OpenLibrary; the
    // returned base may differ from the main task's base under
    // Roadshow/Miami/amiberry default semantics. Capture it, install
    // tc_Launch so each dispatch into this worker swaps the global
    // to its own base, and set the global right now (we're on the
    // worker task) so calls between here and the first
    // context-switch see the right one.
    thread = current_amlang_thread();
    if (thread == NULL) {
        // Main task — already covered by amisslauto. Nothing to do.
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
    // Hand control back to AmLang so the Thread.addFinalizer call is
    // a real lambda registration through the normal codegen path,
    // rather than a C-side handle we'd have to invent a finalizer-
    // dispatch protocol for.
    Am_Net_Socket_f_nativeInit_0();
    return 1;
}

// Counterpart to the per-task `OpenLibrary` above. Reached via the
// Thread finalizer that Socket.nativeInit() registered, so the call
// site is on the same task that opened.
//
// Two things to undo:
//   1. Clear tc_Launch + TF_LAUNCH so the dispatcher stops firing
//      our handler — both because the node is about to be freed,
//      and because the task itself is exiting (Exec doesn't strictly
//      need us to clear, but a dangling handler pointer with the
//      flag still set is a foot-gun).
//   2. CloseLibrary on the PER-TASK base captured in the node — not
//      the global SocketBase, since those may differ under
//      Roadshow/Miami/amiberry default semantics.
//
// We deliberately do NOT touch the global SocketBase here. That's
// the main task's tc_Launch handler's job — every dispatch INTO
// main sets `SocketBase = main_socket_base` automatically (see
// bsdsocket_launch). Touching the global from the worker's
// finalizer would race with main if main was already running:
// main's tc_Launch would have just set it correctly, and our store
// from the worker could undo that before main reaches the next
// socket call.
// --------- Open-fd registry ------------------------------------------------
//
// Mirrors the libc Socket.c registry, but uses `Forbid()`/`Permit()`
// for mutual exclusion (no pthreads on m68k AmigaOS classic) and
// `CloseSocket()` instead of `close()` because bsdsocket fds aren't
// in libnix's DOS-handle table. See the libc file for the rationale
// on why the list lives in C instead of being a List<Socket> on the
// AmLang side.

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

// Compiler-injected via `#runOnExit` on the AmLang side. Closes every
// fd we registered and never saw `CloseSocket()`'d. CloseSocket from
// the main task interrupts whatever the worker task is doing with the
// fd — its `recv()`/`send()` returns -1 with errno EBADF and the
// resulting exception propagates back through the suspend chain.
function_result Am_Net_Socket_closeAllOpenFds_0(void)
{
    function_result __result = { .has_return_value = false };
    // Snapshot under the Forbid so we don't hold the lock across a
    // CloseSocket round-trip (which can stall briefly).
    int count;
    int fds[AM_NET_MAX_OPEN_FDS];
    Forbid();
    count = s_open_fds_count;
    for (int i = 0; i < count; i++) {
        fds[i] = s_open_fds[i];
    }
    s_open_fds_count = 0;
    Permit();
    // CloseSocket only — bsdsocket doesn't have shutdown() in the
    // proto/socket.h vector here, and CloseSocket alone is sufficient
    // to make any blocked recv()/send() in the worker task wake.
    if (SocketBase != NULL) {
        for (int i = 0; i < count; i++) {
            CloseSocket(fds[i]);
        }
    }
    return __result;
}

function_result Am_Net_Socket_closeBsdsocketForThread_0()
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
    // OS fd and remove it from the shutdown registry. A fd of -1 means
    // close() already ran, which is the normal path.
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

    s = socket(addressFamily, socketType, protocolFamily);
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

    he = gethostbyname((STRPTR)host_name_holder->string_value);
    if (!he) {
        __throw_simple_exception("Unable to resolve host", "in Am_Net_Socket_connectNative_0", &__result);
        goto __exit;
    }

    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_addr   = *(struct in_addr *)he->h_addr_list[0];
    peer_addr.sin_family = addressFamily;
    peer_addr.sin_port   = htons(port);
    peer_addr.sin_len    = he->h_length;  // bsdsocket-on-Amiga expects
                                          // a non-zero sin_len; mirrors
                                          // AmiSSL's test/https.c.

    s = this->object_properties.class_object_properties.object_data.value.int_value;
    printf("Socket.connect: calling connect() s=%d\n", s); fflush(stdout);
    result = connect(s, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    printf("Socket.connect: connect() returned %d\n", result); fflush(stdout);
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
}

// Detect this machine's own outbound IP via UDP-connect + getsockname,
// against bsdsocket.library. See the AmLang doc-comment on
// Socket.getLocalIpAddress. Returns the dotted-quad string, or "" when
// there's no route / bsdsocket can't open (no TCP-IP stack running).
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
			peer.sin_len    = sizeof(struct in_addr);  // bsdsocket wants non-zero
			// Any routable address — UDP connect sends nothing, it just
			// forces a route lookup so getsockname reports our interface.
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
