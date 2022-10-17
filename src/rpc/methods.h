#ifndef ACC_ENGINEER_SERVER_RPC_METHODS_H
#define ACC_ENGINEER_SERVER_RPC_METHODS_H

// std
#include <unordered_map>

// spdlog
#include <spdlog/spdlog.h>

// boost
#include <boost/asio/awaitable.hpp>

#include "detail/method_type_erasure.h"
#include "types.h"

namespace acc_engineer::rpc
{
    template<method_message Message, typename AwaitableMethodImplement>
    requires std::invocable<AwaitableMethodImplement, const request_t<Message> &> &&
             std::same_as<
                     std::invoke_result_t<AwaitableMethodImplement, const request_t<Message> &>,
                     net::awaitable<response_t<Message>>>
    class method final : public detail::method_type_erasure
    {
    public:
        explicit method(AwaitableMethodImplement &&implement) : implement_(
                std::forward<AwaitableMethodImplement>(implement))
        {
        }

        net::awaitable<result<payload_t>> operator()(payload_t request_message_payload) override
        {
            request_t<Message> request{};
            if (!request.ParseFromString(request_message_payload.data()))
            {
                spdlog::error("method invoke error, parse request fail");
                co_return system_error::proto_parse_fail;
            }

            response_t<Message> response = co_await std::invoke(implement_, std::cref(request));
            spdlog::debug(
                    R"(method invoke method: {} request: [{}] response: [{}])",
                    Message::descriptor()->full_name(),
                    request.ShortDebugString(),
                    response.ShortDebugString());

            std::string response_message_payload;
            if (!response.SerializeToString(&response_message_payload))
            {
                spdlog::error("method invoke error, serialize response fail");
                co_return system_error::proto_serialize_fail;
            }

            co_return std::move(response_message_payload);
        }

    private:
        AwaitableMethodImplement implement_;
    };

    class method_group : public boost::noncopyable
    {
    public:
        static method_group &empty_method_group()
        {
            static method_group group;
            return group;
        }

        template<method_message Message, typename AwaitableMethodImplement>
        requires std::invocable<AwaitableMethodImplement, const request_t<Message> &> &&
                 std::same_as<
                         std::invoke_result_t<AwaitableMethodImplement, const request_t<Message> &>,
                         net::awaitable<response_t<Message>>>
        method_group &implement(uint64_t command_id, AwaitableMethodImplement &&implement)
        {
            if (implements_.contains(command_id))
            {
                throw std::runtime_error("cmd_id already registered");
            }

            implements_.emplace(command_id, new method<Message, AwaitableMethodImplement>(std::forward<AwaitableMethodImplement>(implement)));

            return *this;
        }

        net::awaitable<result<payload_t>> operator()(uint64_t command_id, payload_t request_message_payload) const
        {
            const auto implement = implements_.find(command_id);
            if (implement == implements_.end())
            {
                spdlog::error("method_group invoke error, no such implement cmd_id {}", command_id);
                co_return system_error::method_not_implement;
            }

            co_return co_await std::invoke(*implement->second, std::move(request_message_payload));
        }

    private:

        std::unordered_map<uint64_t, std::unique_ptr<detail::method_type_erasure>> implements_;
    };
}

#endif
