#ifndef ACC_ENGINEER_SERVER_RPC_STUB_H
#define ACC_ENGINEER_SERVER_RPC_STUB_H

#include "detail/stream_stub.h"
#include "detail/datagram_stub.h"

namespace acc_engineer::rpc
{
    using detail::stream_stub;
    using detail::datagram_stub;
}// namespace acc_engineer::rpc

#endif//ACC_ENGINEER_SERVER_RPC_STUB_H
