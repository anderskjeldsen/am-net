#include <libc/core.h>
#include <Am/Net/Socket.h>
#include <libc/Am/Net/Socket.h>
#include <Am/Lang/Object.h>
#include <Am/Net/AddressFamily.h>
#include <libc/core_inline_functions.h>

#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

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

	printf("Binding socket %d to port %d\n", s, port);
	
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

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	if (clientSocket != NULL) {
		__decrease_reference_count(clientSocket);
	}
	return __result;
};


