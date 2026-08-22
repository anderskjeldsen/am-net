#include <libc/core.h>
#include <Am/Net/Socket.h>
#include <macos/Am/Net/Socket.h>
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
