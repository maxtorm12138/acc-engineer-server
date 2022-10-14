#ifndef ACC_ENGINEER_SERVER_DETAIL_METHOD_BASE_H
#define ACC_ENGINEER_SERVER_DETAIL_METHOD_BASE_H

#include <boost/asio/awaitable.hpp>

#include "types.h"

namespace acc_engineer::rpc::detail
{
    struct method_base
    {
        virtual boost::asio::awaitable<std::string> operator()(uint64_t cmd_id, request_id_type request_id, std::string request_payload) = 0;

        virtual ~method_base() = default;
    };


    template<RPCMethodMessage RPCMessage, typename AwaitableRPCMethod>
    requires std::invocable<AwaitableRPCMethod, request_id_type, rpc_request_t<RPCMessage>> &&
             std::same_as<
                     std::invoke_result_t<
                             AwaitableRPCMethod, request_id_type, rpc_request_t<RPCMessage>>,
                     boost::asio::awaitable<rpc_response_t<RPCMessage>>
             >
    class method : public method_base
    {
    public:
        using request_type = rpc_request_t<RPCMessage>;
        using response_type = rpc_response_t<RPCMessage>;
        using method_type = AwaitableRPCMethod;
    public:
        explicit method(method_type &&rpc_method) :
                rpc_method_(std::forward<method_type>(rpc_method))
        {}

        virtual boost::asio::awaitable<std::string> operator()(uint64_t cmd_id, request_id_type request_id, std::string request_payload) override
        {
            request_type request;
            request.ParseFromString(request_payload);
            response_type response = co_await std::invoke(rpc_method_, request_id, std::cref(request));
            co_return response.SerializeAsString();
        }

    private:
        method_type rpc_method_;
    };
}
#endif //ACC_ENGINEER_SERVER_DETAIL_METHOD_BASE_H
