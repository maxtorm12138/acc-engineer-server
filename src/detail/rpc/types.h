#ifndef ACC_ENGINEER_SERVER_DETAIL_RPC_TYPES_H
#define ACC_ENGINEER_SERVER_DETAIL_RPC_TYPES_H

#include <array>
#include <concepts>
#include <boost/asio/awaitable.hpp>
#include <google/protobuf/message.h>

namespace acc_engineer::rpc
{
    using request_id_type = std::array<uint8_t, 16>;

    template<typename message>
    concept RPCMethodMessage =
    requires
    {
        std::derived_from<typename message::Response, google::protobuf::Message>;
        std::derived_from<typename message::Request, google::protobuf::Message>;
    };

    template<RPCMethodMessage T>
    struct rpc_request
    {
        using type = typename T::Request;
    };

    template<RPCMethodMessage T>
    using rpc_request_t = typename rpc_request<T>::type;

    template<RPCMethodMessage T>
    struct rpc_response
    {
        using type = typename T::Response;
    };

    template<RPCMethodMessage T>
    using rpc_response_t = typename rpc_response<T>::type;

}

#endif //ACC_ENGINEER_SERVER_DETAIL_RPC_TYPES_H
