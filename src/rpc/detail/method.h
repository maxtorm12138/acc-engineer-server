#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/noncopyable.hpp>

// spdlog
#include <spdlog/spdlog.h>

#include "error_code.h"
#include "type_requirements.h"
#include "types.h"

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;

struct method_type_erasure
{
    method_type_erasure() = default;
    method_type_erasure(const method_type_erasure &) = delete;
    method_type_erasure &operator=(const method_type_erasure &) = delete;

    virtual net::awaitable<std::string> operator()(context_t context, std::string request_message_payload) = 0;

    virtual ~method_type_erasure() = default;
};

template<method_message Message, typename Implement>
    requires method_implement<Message, Implement, context_t>
class method final : public method_type_erasure
{
public:
    explicit method(Implement &&implement);

    net::awaitable<std::string> operator()(context_t context, std::string request_message_payload) override;

private:
    Implement implement_;
};

template<method_message Message, typename Implement>
    requires method_implement<Message, Implement, context_t>
method<Message, Implement>::method(Implement &&implement)
    : implement_(std::forward<Implement>(implement))
{}

template<method_message Message, typename Implement>
    requires method_implement<Message, Implement, context_t>
net::awaitable<std::string> method<Message, Implement>::operator()(context_t context, std::string request_message_payload)
{
    request_t<Message> request{};
    if (!request.ParseFromString(request_message_payload.data()))
    {
        spdlog::error("method invoke error, parse request fail");
        throw sys::system_error(system_error::proto_parse_fail);
    }

    response_t<Message> response = co_await std::invoke(implement_, std::cref(context), std::cref(request));

    std::string response_message_payload;
    if (!response.SerializeToString(&response_message_payload))
    {
        spdlog::error("method invoke error, serialize response fail");
        throw sys::system_error(system_error::proto_serialize_fail);
    }

    co_return std::move(response_message_payload);
}

class method_group : public boost::noncopyable
{
public:
    static method_group &empty_method_group()
    {
        static method_group group;
        return group;
    }

    template<detail::method_message Message, typename Implement>
        requires detail::method_implement<Message, Implement, context_t>
    method_group &implement(Implement &&implement)
    {
        uint64_t command_id = Message::descriptor()->options().GetExtension(cmd_id);
        if (implements_.contains(command_id))
        {
            throw std::runtime_error(fmt::format("cmd_id {} already registered", command_id));
        }

        implements_.emplace(command_id, new method<Message, Implement>(std::forward<Implement>(implement)));

        return *this;
    }

    net::awaitable<std::string> operator()(uint64_t command_id, context_t context, std::string request_message_payload) const
    {
        const auto implement = implements_.find(command_id);
        if (implement == implements_.end())
        {
            spdlog::error("method_group invoke error, no such implement cmd_id {}", command_id);
            throw sys::system_error(detail::system_error::method_not_implement);
        }

        co_return co_await std::invoke(*implement->second, std::move(context), std::move(request_message_payload));
    }

private:
    std::unordered_map<uint64_t, std::unique_ptr<detail::method_type_erasure>> implements_;
};

} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
