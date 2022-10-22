#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H

// std
#include <unordered_map>
#include <span>

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/noncopyable.hpp>

// spdlog
#include <spdlog/spdlog.h>

// module
#include "rpc/detail/error_code.h"

// proto
#include "proto/rpc.pb.h"

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;

template<typename Message>
concept is_method_message = requires
{
    std::derived_from<Message, google::protobuf::Message>;
    std::derived_from<typename Message::Response, google::protobuf::Message>;
    std::derived_from<typename Message::Request, google::protobuf::Message>;
};

template<typename MethodMessage, typename MethodImplement, typename Context>
concept is_method_implement = requires(MethodImplement implement, const typename MethodMessage::Request &request, const Context &context)
{
    {is_method_message<MethodMessage>};
    {std::is_move_constructible_v<MethodImplement>};
    {std::is_move_assignable_v<MethodImplement>};
    {
        std::invoke(implement, context, request)
        } -> std::same_as<net::awaitable<typename MethodMessage::Response>>;
};

template<is_method_message Message>
struct request
{
    using type = typename Message::Request;
};

template<is_method_message Message>
using request_t = typename request<Message>::type;

template<is_method_message Message>
struct response
{
    using type = typename Message::Response;
};

struct context_t
{
    uint64_t stub_id;
    uint64_t packet_handler_type;
};

template<is_method_message message>
using response_t = typename response<message>::type;

struct method_type_erasure : public boost::noncopyable
{
    virtual net::awaitable<std::vector<uint8_t>> operator()(context_t context, std::span<uint8_t> request_message_payload) = 0;

    virtual ~method_type_erasure() = default;
};

template<is_method_message Message, typename Implement>
requires is_method_implement<Message, Implement, context_t>
class method final : public method_type_erasure
{
public:
    explicit method(Implement &&implement);

    net::awaitable<std::vector<uint8_t>> operator()(context_t context, std::span<uint8_t> request_message_payload) override;

private:
    Implement implement_;
};

template<is_method_message Message, typename Implement>
requires is_method_implement<Message, Implement, context_t> method<Message, Implement>::method(Implement &&implement)
    : implement_(std::forward<Implement>(implement))
{}

template<is_method_message Message, typename Implement>
requires is_method_implement<Message, Implement, context_t> net::awaitable<std::vector<uint8_t>> method<Message, Implement>::operator()(
    context_t context, std::span<uint8_t> request_message_payload)
{
    request_t<Message> request{};
    if (!request.ParseFromArray(request_message_payload.data(), request_message_payload.size()))
    {
        spdlog::error("method invoke error, parse request fail");
        throw sys::system_error(system_error::proto_parse_fail);
    }

    response_t<Message> response = co_await std::invoke(implement_, std::cref(context), std::cref(request));

    std::vector<uint8_t> response_message_payload(response.ByteSizeLong());
    if (!response.SerializeToArray(response_message_payload.data(), response_message_payload.size()))
    {
        spdlog::error("method invoke error, serialize response fail");
        throw sys::system_error(system_error::proto_serialize_fail);
    }

    co_return response_message_payload;
}

class methods : public boost::noncopyable
{
public:
    static methods &empty()
    {
        static methods methods;
        return methods;
    }

    template<is_method_message Message, typename Implement>
    requires is_method_implement<Message, Implement, context_t> methods &implement(Implement &&implement)
    {
        uint64_t command_id = Message::descriptor()->options().GetExtension(cmd_id);
        if (implements_.contains(command_id))
        {
            spdlog::critical("methods::implement runtime_error |{}|already registered|", command_id);
            throw std::runtime_error(fmt::format("cmd_id {} already registered", command_id));
        }

        implements_.emplace(command_id, new method<Message, Implement>(std::forward<Implement>(implement)));

        return *this;
    }

    net::awaitable<std::vector<uint8_t>> operator()(uint64_t command_id, context_t context, std::span<uint8_t> request_message_payload) const
    {
        const auto implement = implements_.find(command_id);
        if (implement == implements_.end())
        {
            spdlog::error("methods::operator() system_error |{}|method not implement|", command_id);
            throw sys::system_error(detail::system_error::method_not_implement);
        }

        return std::invoke(*implement->second, std::move(context), std::move(request_message_payload));
    }

private:
    std::unordered_map<uint64_t, std::unique_ptr<method_type_erasure>> implements_;
};

} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
