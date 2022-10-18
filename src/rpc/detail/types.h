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

namespace acc_engineer::rpc::detail
{
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
        flag_is_request = 0
    };

    using reply_channel_t = net::experimental::channel<void(sys::error_code, rpc::Cookie, std::string)>;
    using sender_channel_t = net::experimental::channel<
    void(sys::error_code,
    const std::string *)>;

    constexpr uint64_t MAX_PAYLOAD_SIZE = 1400;
}

#endif //ACC_ENGINEER_SERVER_RPC_DETAIL_TYPES_H
