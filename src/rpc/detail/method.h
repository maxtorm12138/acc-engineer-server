#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H

// std
#include <unordered_map>
#include <span>

// boost
#include <boost/system/error_code.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/noncopyable.hpp>

// spdlog
#include <spdlog/spdlog.h>

// module
#include "error_code.h"

// proto
#include "proto/rpc.pb.h"

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;

struct context
{
    uint64_t stub_id;
    uint64_t packet_handler_type;
};

template<typename Message>
concept is_method_message = requires {
                                std::derived_from<Message, google::protobuf::Message>;
                                std::derived_from<typename Message::Response, google::protobuf::Message>;
                                std::derived_from<typename Message::Request, google::protobuf::Message>;
                            };

template<typename MethodMessage, typename MethodImplement>
concept is_method_implement =
    requires {
        is_method_message<MethodMessage>;
        std::is_move_constructible_v<MethodImplement>;
        std::is_move_assignable_v<MethodImplement>;
        std::same_as<std::invoke_result<MethodImplement, const context &, const typename MethodMessage::Request &>, net::awaitable<typename MethodMessage::Response>>;
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

template<is_method_message message>
using response_t = typename response<message>::type;

struct method_type_erasure : public boost::noncopyable
{
    virtual net::awaitable<std::vector<uint8_t>> operator()(context context, std::span<uint8_t> request_message_payload, sys::error_code &error_code) noexcept = 0;

    virtual ~method_type_erasure() = default;
};

template<is_method_message Message, typename Implement>
    requires is_method_implement<Message, Implement>
class method final : public method_type_erasure
{
public:
    explicit method(Implement &&implement)
        : implement_(std::forward<Implement>(implement)){};

    net::awaitable<std::vector<uint8_t>> operator()(context context, std::span<uint8_t> request_payload, sys::error_code &error_code) noexcept override
    {
        try
        {
            SPDLOG_DEBUG("method {} invoke", Message::descriptor()->full_name());
            request_t<Message> request{};
            if (!request.ParseFromArray(request_payload.data(), static_cast<int>(request_payload.size())))
            {
                throw sys::system_error(system_error::proto_parse_fail);
            }

            response_t<Message> response = co_await std::invoke(implement_, std::cref(context), std::cref(request));

            std::vector<uint8_t> response_payload(response.ByteSizeLong());
            if (!response.SerializeToArray(response_payload.data(), static_cast<int>(response_payload.size())))
            {
                throw sys::system_error(system_error::proto_serialize_fail);
            }

            co_return response_payload;
        }
        catch (const sys::system_error &ex)
        {
            SPDLOG_ERROR("method {} invoke system_error: {}", Message::descriptor()->full_name(), ex.what());
            if (ex.code().category() == system_error_category())
            {
                error_code = ex.code();
                co_return std::vector<uint8_t>{};
            }
            else
            {
                error_code = system_error::unhandled_system_error;
                co_return std::vector<uint8_t>{};
            }
        }
        catch (const std::exception &ex)
        {
            SPDLOG_ERROR("method {} invoke exception: {}", Message::descriptor()->full_name(), ex.what());
            error_code = system_error::unhandled_exception;
            co_return std::vector<uint8_t>{};
        }
    }

private:
    Implement implement_;
};

class methods : public boost::noncopyable
{
public:
    static methods &empty()
    {
        static methods methods;
        return methods;
    }

    template<is_method_message Message, typename Implement>
        requires is_method_implement<Message, Implement>
    methods &implement(Implement &&implement)
    {
        uint64_t command_id = Message::descriptor()->options().GetExtension(cmd_id);
        if (implements_.contains(command_id))
        {
            SPDLOG_CRITICAL("methods {} command_id {} already registered", Message::descriptor()->full_name(), command_id);
            throw std::runtime_error(fmt::format("cmd_id {} already registered", command_id));
        }

        implements_.emplace(command_id, new method<Message, Implement>(std::forward<Implement>(implement)));

        return *this;
    }

    net::awaitable<std::vector<uint8_t>> operator()(uint64_t command_id, context context, std::span<uint8_t> request_message_payload, sys::error_code &error_code) const noexcept
    {
        const auto implement = implements_.find(command_id);
        if (implement == implements_.end())
        {
            SPDLOG_ERROR("methods::operator() system_error |{}|method not implement|", command_id);
            error_code = system_error::method_not_implement;
            co_return std::vector<uint8_t>{};
        }

        co_return co_await std::invoke(*implement->second, context, request_message_payload, error_code);
    }

private:
    std::unordered_map<uint64_t, std::unique_ptr<method_type_erasure>> implements_;
};

} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
