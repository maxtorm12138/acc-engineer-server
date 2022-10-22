#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_TYPE_REQUIREMENTS_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_TYPE_REQUIREMENTS_H

// std
#include <concepts>

// protobuf
#include <google/protobuf/message.h>

// boost
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace acc_engineer::rpc::detail {

namespace net = boost::asio;
namespace sys = boost::system;

} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_TYPE_REQUIREMENTS_H
