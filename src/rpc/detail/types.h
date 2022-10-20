#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_TYPES_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_TYPES_H

// std
#include <string>

// boost
#include <boost/system/error_code.hpp>
#include <boost/asio/experimental/channel.hpp>

// module
#include "type_requirements.h"

// protocol
#include "proto/rpc.pb.h"

namespace acc_engineer::rpc::detail {
template<method_message Message>
struct request
{
    using type = typename Message::Request;
};

template<method_message Message>
using request_t = typename request<Message>::type;

template<method_message Message>
struct response
{
    using type = typename Message::Response;
};

template<method_message Message>
using response_t = typename response<Message>::type;

enum message_flags
{
    flag_is_request = 0,
    flag_no_reply = 1
};

enum class stub_type
{
    stream = 1,
    datagram = 2
};

enum class stub_status
{
    idle = 1,
    running = 2,
    stopping = 3,
    stopped = 4,
};

using reply_channel_t = net::experimental::channel<void(sys::error_code, Cookie, std::string)>;
using sender_channel_t = net::experimental::channel<void(sys::error_code, std::string)>;
using deliver_channel_t = net::experimental::channel<void(sys::error_code, std::string)>;
using stopping_channel_t = net::experimental::channel<void(sys::error_code, uint64_t)>;
using duration_t = std::chrono::steady_clock::duration;

struct context_t
{
    uint64_t stub_id;
    stub_type stub_type;
};

constexpr uint64_t MAX_PAYLOAD_SIZE = 1400;
} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_TYPES_H
