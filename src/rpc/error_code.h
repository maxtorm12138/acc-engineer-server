#ifndef ACC_ENGINEER_SERVER_RPC_ERROR_CODE_H
#define ACC_ENGINEER_SERVER_RPC_ERROR_CODE_H

#include "detail/error_code.h"

namespace acc_engineer::rpc
{
	using detail::system_error;
	using detail::make_error_code;
}

#endif
