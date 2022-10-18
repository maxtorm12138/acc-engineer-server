#ifndef ACC_ENGINEER_SERVER_RPC_METHODS_H
#define ACC_ENGINEER_SERVER_RPC_METHODS_H

// std
#include <unordered_map>

// spdlog
#include <spdlog/spdlog.h>

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/noncopyable.hpp>

#include "detail/method_type_erasure.h"
#include "detail/method.h"
#include "detail/type_requirements.h"

namespace acc_engineer::rpc
{
    namespace net = boost::asio;

    class method_group : public boost::noncopyable
    {
    public:
        static method_group &empty_method_group()
        {
            static method_group group;
            return group;
        }

        template<detail::method_message Message, typename Implement>
        requires detail::method_implement<Message, Implement>
        method_group &implement(uint64_t command_id, Implement &&implement)
        {
            if (implements_.contains(command_id))
            {
                throw std::runtime_error("cmd_id already registered");
            }

            implements_.emplace(command_id, new detail::method<Message, Implement>(std::forward<Implement>(implement)));

            return *this;
        }

        net::awaitable<result<std::string>> operator()(uint64_t command_id, std::string request_message_payload) const
        {
            const auto implement = implements_.find(command_id);
            if (implement == implements_.end())
            {
                spdlog::error("method_group invoke error, no such implement cmd_id {}", command_id);
                co_return detail::system_error::method_not_implement;
            }

            co_return co_await std::invoke(*implement->second, std::move(request_message_payload));
        }

    private:

        std::unordered_map<uint64_t, std::unique_ptr<detail::method_type_erasure>> implements_;
    };
}

#endif
