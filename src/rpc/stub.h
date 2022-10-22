#ifndef ACC_ENGINEER_SERVER_RPC_STUB_H
#define ACC_ENGINEER_SERVER_RPC_STUB_H

#include "detail/stub.h"
#include "detail/packet_handler.h"

namespace acc_engineer::rpc {

namespace net = boost::asio;

using tcp_stub = detail::stub<detail::tcp_packet_handler>;
using udp_stub = detail::stub<detail::udp_packet_handler>;

} // namespace acc_engineer::rpc

#endif // ACC_ENGINEER_SERVER_RPC_STUB_H
