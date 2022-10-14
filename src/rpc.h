#ifndef ACC_ENGINEER_SERVER_RPC_H
#define ACC_ENGINEER_SERVER_RPC_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/redirect_error.hpp>

#include <boost/core/noncopyable.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/scope_exit.hpp>

#include "detail/rpc/method.h"

#include "service.pb.h"


namespace acc_engineer
{
    namespace net = boost::asio;
    namespace sys = boost::system;
}

namespace acc_engineer::rpc
{
    using request_id_t = std::array<uint8_t, 16>;
}


namespace acc_engineer::rpc
{

    using message_channel_t = net::experimental::channel<void(sys::error_code, uint64_t, std::array<uint8_t, 16>, std::string)>;

    template<typename T>
    class result
    {
    public:
        result(T &&value) : value_(std::forward<T>(value))
        {}

        result(sys::error_code ec) : error_(ec)
        {}

        result(const T &value) : value_(value)
        {}

        sys::error_code error()
        {
            return error_;
        };

        T &value()
        {
            return *value_;
        }

        const T &value() const
        {
            return *value_;
        }

        T &&take()
        {
            return std::move(*value_);
        }

    private:
        std::optional<T> value_;
        sys::error_code error_{};
    };

    class client_service : public boost::noncopyable
    {
    public:
        explicit client_service(net::ip::tcp::socket &socket);

    public:
        net::awaitable<void> run();

        template<typename Method>
        net::awaitable<result<Method>> async_call(Method &method)
        {
            auto executor = co_await net::this_coro::executor;

            auto request_id = generate_request_id();
            auto cmd_id = Method::descriptor()->options().GetExtension(proto::cmd_id);

            auto receive_channel = std::make_shared<message_channel_t>(executor, 1);

            auto[iter, exists] = calling_.emplace(request_id, receive_channel);

            sys::error_code ec;
            co_await send_channel_->async_send({}, cmd_id, request_id, method.SerializeAsString(), net::redirect_error(net::use_awaitable, ec));
            if (ec)
            {
                calling_.erase(request_id);
                co_return result<Method>(ec);
            }

            auto[_, _1, response_payloads] = co_await receive_channel->async_receive(net::redirect_error(net::use_awaitable, ec));
            if (ec)
            {
                calling_.erase(request_id);
                co_return result<Method>(ec);
            }

            Method response;
            response.ParseFromString(response_payloads);

            calling_.erase(request_id);

            co_return result<Method>(response);
        }

    private:

        request_id_t generate_request_id();

        net::awaitable<void> receiver();

        net::awaitable<void> sender();

    private:
        net::ip::tcp::socket &socket_;
        boost::uuids::random_generator uuid_generator_;
        std::optional<message_channel_t> send_channel_;
        std::map<request_id_t, std::shared_ptr<message_channel_t>> calling_;
    };

    class server_service : public boost::noncopyable
    {
    public:
        template<typename MethodMessage, typename AwaitableMethod>
        static void register_method(uint64_t cmd_id, AwaitableMethod &&m)
        {
            std::unique_ptr<detail::method_base> real_method(new detail::method<MethodMessage, AwaitableMethod>(std::forward<AwaitableMethod>(m)));
            methods_.emplace(cmd_id, std::move(real_method));
        }

    public:
        explicit server_service(net::ip::tcp::socket &socket);

    public:
        net::awaitable<void> run();

        net::awaitable<void> invoke(uint64_t cmd_id, request_id_t request_id, std::string request_payload);

    private:
        static std::unordered_map<uint64_t, std::unique_ptr<detail::method_base>> methods_;
        net::ip::tcp::socket &socket_;
    };
}

#endif // !ACC_ENGINEER_SERVER_RPC_H
