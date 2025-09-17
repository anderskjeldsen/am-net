#include <libc/core.h>
#include <Am/Net/UdpSocket.h>
#include <libc/Am/Net/UdpSocket.h>
#include <Am/Lang/Object.h>
#include <Am/Net/AddressFamily.h>
#include <libc/core_inline_functions.h>

#include <unistd.h>
#include <string.h>

function_result Am_Net_UdpSocket__native_init_0(aobject * const this)
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

function_result Am_Net_UdpSocket__native_release_0(aobject * const this)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
__exit: ;
	return __result;
};

function_result Am_Net_UdpSocket__native_mark_children_0(aobject * const this)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
__exit: ;
	return __result;
};

function_result Am_Net_UdpSocket_createSocket_0(aobject * const this, int addressFamily)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}

	int s = socket(addressFamily, SOCK_DGRAM, 0);
	if (s < 0)
	{
		__throw_simple_exception("Unable to create UDP socket", "in Am_Net_UdpSocket_createSocket_0", &__result);
		goto __exit;
	}

	this->object_properties.class_object_properties.object_data.value.int_value = s;

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	return __result;
};

function_result Am_Net_UdpSocket_bind_0(aobject * const this, int port)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	if (s < 0)
	{
		__throw_simple_exception("UDP socket not created", "in Am_Net_UdpSocket_bind_0", &__result);
		goto __exit;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		__throw_simple_exception("Unable to bind UDP socket", "in Am_Net_UdpSocket_bind_0", &__result);
		goto __exit;
	}

__exit: ;
	if (this != NULL) {
		__decrease_reference_count(this);
	}
	return __result;
};

function_result Am_Net_UdpSocket_sendTo_0(aobject * const this, aobject * bytes, const unsigned int length, aobject * hostName, int port)
{
	function_result __result = { .has_return_value = true };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}
	if (bytes != NULL) {
		__increase_reference_count(bytes);
	}
	if (hostName != NULL) {
		__increase_reference_count(hostName);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	if (s < 0)
	{
		__throw_simple_exception("UDP socket not created", "in Am_Net_UdpSocket_sendTo_0", &__result);
		goto __exit;
	}

	array_holder *a_holder = (array_holder *) &bytes[1];
	if (length > a_holder->size) {
		__throw_simple_exception("Send length is bigger than array", "in Am_Net_UdpSocket_sendTo_0", &__result);
		goto __exit;
	}

	string_holder *host_name_holder = hostName->object_properties.class_object_properties.object_data.value.custom_value;
	
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	struct hostent * hostent = gethostbyname(host_name_holder->string_value);
	if (hostent)
	{
		addr.sin_addr = *(struct in_addr *) hostent->h_addr_list[0];
	}
	else
	{
		__throw_simple_exception("Unable to resolve hostname", "in Am_Net_UdpSocket_sendTo_0", &__result);
		goto __exit;
	}

	int sent = sendto(s, a_holder->array_data, length, 0, (struct sockaddr*)&addr, sizeof(addr));
	if (sent < 0)
	{
		__throw_simple_exception("Error sending UDP data", "in Am_Net_UdpSocket_sendTo_0", &__result);
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
	if (hostName != NULL) {
		__decrease_reference_count(hostName);
	}
	return __result;
};

function_result Am_Net_UdpSocket_receiveFrom_0(aobject * const this, aobject * bytes, const unsigned int length)
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
	if (s < 0)
	{
		__throw_simple_exception("UDP socket not created", "in Am_Net_UdpSocket_receiveFrom_0", &__result);
		goto __exit;
	}

	array_holder *a_holder = (array_holder *) &bytes[1];
	if (length > a_holder->size) {
		__throw_simple_exception("Receive length is bigger than array", "in Am_Net_UdpSocket_receiveFrom_0", &__result);
		goto __exit;
	}

	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int received = recvfrom(s, a_holder->array_data, length, 0, (struct sockaddr*)&addr, &addr_len);

	if (received < 0)
	{
		__throw_simple_exception("Error receiving UDP data", "in Am_Net_UdpSocket_receiveFrom_0", &__result);
		goto __exit;
	}

	__result.return_value.value.uint_value = received;
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

function_result Am_Net_UdpSocket_close_0(aobject * const this)
{
	function_result __result = { .has_return_value = false };
	bool __returning = false;
	if (this != NULL) {
		__increase_reference_count(this);
	}

	int s = this->object_properties.class_object_properties.object_data.value.int_value;
	if (s < 0)
	{
		__throw_simple_exception("UDP socket not created", "in Am_Net_UdpSocket_close_0", &__result);
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