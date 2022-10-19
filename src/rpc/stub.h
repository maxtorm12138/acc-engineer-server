#ifndef ACC_ENGINEER_SERVER_RPC_STUB_H
#define ACC_ENGINEER_SERVER_RPC_STUB_H

// std
#include <bitset>
#include <numeric>

// log
#include <spdlog/spdlog.h>

// boost
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

// module
#include "detail/type_requirements.h"
#include "detail/stub_base.h"
#include "detail/types.h"
#include "method_group.h"

namespace acc_engineer::rpc
{
    namespace sys = boost::system;
    namespace net = boost::asio;
    using namespace std::chrono_literals;

    template<detail::async_datagram MethodChannel>
    class datagram_stub : public detail::stub_base
    {
    public:
        explicit datagram_stub(MethodChannel method_channel, const method_group &method_group = method_group::empty_method_group());

        net::awaitable<void> run(net::const_buffer initial = {});

        template<detail::method_message Message>
        net::awaitable<detail::response_t<Message>> async_call(const detail::request_t<Message> &request, detail::duration_t timeout = 100s);

    private:

        net::awaitable<void> sender_loop();

        net::awaitable<void> receiver_loop();

        net::awaitable<void> handle_initial(net::const_buffer initial);

        MethodChannel method_channel_;
        detail::sender_channel_t sender_channel_;
    };

    template<detail::async_stream MethodChannel>
    class stream_stub : public detail::stub_base
    {
    public:
        explicit stream_stub(MethodChannel method_channel, const method_group &method_group = method_group::empty_method_group());

        net::awaitable<void> run();

        template<detail::method_message Message>
        net::awaitable<detail::response_t<Message>> async_call(const detail::request_t<Message> &request, detail::duration_t timeout = 100s);

    private:

        net::awaitable<void> sender_loop();

        net::awaitable<void> receiver_loop();

        MethodChannel method_channel_;
        detail::sender_channel_t sender_channel_;
    };

    template<detail::async_datagram MethodChannel>
    datagram_stub<MethodChannel>::datagram_stub(MethodChannel method_channel, const method_group &method_group) :
            stub_base(method_group),
            method_channel_(std::move(method_channel)),
            sender_channel_(method_channel_.get_executor())
    {}

    template<detail::async_datagram MethodChannel>
    net::awaitable<void> datagram_stub<MethodChannel>::run(net::const_buffer initial)
    {
        status_ = detail::stub_status::running;
        if (initial.size() > 0)
        {
            co_await handle_initial(initial);
        }

        net::co_spawn(co_await net::this_coro::executor, sender_loop(), net::detached);
        net::co_spawn(co_await net::this_coro::executor, receiver_loop(), net::detached);
    }

    template<detail::async_datagram MethodChannel>
    net::awaitable<void> datagram_stub<MethodChannel>::sender_loop()
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        net::steady_timer sender_timer(co_await net::this_coro::executor);
        while (status_ == stub_status::running)
        {
            std::string buffers_to_send = co_await this->sender_channel_.async_receive(net::use_awaitable);
            sender_timer.expires_after(500ms);
            co_await (method_channel_.async_send(net::buffer(buffers_to_send), net::use_awaitable) || sender_timer.async_wait(net::use_awaitable));
            spdlog::debug("{} sender_loop send size {}", id(), buffers_to_send.size());
        }
    }

    template<detail::async_datagram MethodChannel>
    net::awaitable<void> datagram_stub<MethodChannel>::receiver_loop()
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;


        while (this->status_ == stub_status::running)
        {
            std::string payload(1500, '\0');
            size_t size_read = co_await method_channel_.async_receive(net::buffer(payload), net::use_awaitable);
            spdlog::debug("{} receiver_loop received size {}", id(), size_read);

            uint64_t payload_size = 0;
            memcpy(&payload_size, payload.data(), sizeof(payload_size));

            // TODO size_read == payload_size
            uint64_t command_id;
            std::bitset<64> flags;
            rpc::Cookie cookie;
            auto message_payload = unpack(net::buffer(payload.data() + sizeof(payload_size), payload_size), command_id, flags, cookie);

            if (std::bitset<64>(flags).test(detail::flag_is_request))
            {
                co_await dispatch_request(sender_channel_, command_id, std::move(cookie), std::move(message_payload));
            }
            else
            {
                co_await dispatch_response(command_id, std::move(cookie), message_payload);
            }
        }
    }

    template<detail::async_datagram MethodChannel>
    net::awaitable<void> datagram_stub<MethodChannel>::handle_initial(net::const_buffer initial)
    {
        uint64_t payload_size = 0;
        std::vector<uint8_t> payload(1500);

        const std::array segment_payload
                {
                        net::buffer(net::buffer(&payload_size, sizeof(payload_size))),
                        net::buffer(payload)
                };

        net::buffer_copy(segment_payload, initial);

        uint64_t command_id;
        std::bitset<64> flags;
        rpc::Cookie cookie;
        auto message_payload = unpack(net::buffer(payload, payload_size), command_id, flags, cookie);

        if (std::bitset<64>(flags).test(detail::flag_is_request))
        {
            co_await dispatch_request(sender_channel_, command_id, std::move(cookie), std::move(message_payload));
        }
    }

    template<detail::async_datagram MethodChannel>
    template<detail::method_message Message>
    net::awaitable<detail::response_t<Message>> datagram_stub<MethodChannel>::async_call(const detail::request_t<Message> &request, detail::duration_t timeout)
    {
        using namespace boost::asio::experimental::awaitable_operators;
        net::steady_timer timeout_timer(co_await net::this_coro::executor);
        timeout_timer.expires_after(timeout);
        std::variant<detail::response_t<Message>, std::monostate> result = co_await (do_async_call<Message>(sender_channel_, request) ||
                                                                                     timeout_timer.async_wait(net::use_awaitable));
        detail::response_t<Message> *message = std::get_if<detail::response_t<Message>>(&result);
        if (message != nullptr)
        {
            co_return std::move(*message);
        }
        throw detail::system_error::call_timeout;
    }

    template<detail::async_stream MethodChannel>
    stream_stub<MethodChannel>::stream_stub(MethodChannel method_channel, const method_group &method_group) :
            stub_base(method_group),
            method_channel_(std::move(method_channel)),
            sender_channel_(method_channel_.get_executor())
    {}

    template<detail::async_stream MethodChannel>
    net::awaitable<void> stream_stub<MethodChannel>::run()
    {
        status_ = detail::stub_status::running;
        net::co_spawn(co_await net::this_coro::executor, sender_loop(), net::detached);
        net::co_spawn(co_await net::this_coro::executor, receiver_loop(), net::detached);
    }

    template<detail::async_stream MethodChannel>
    net::awaitable<void> stream_stub<MethodChannel>::sender_loop()
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        net::steady_timer sender_timer(co_await net::this_coro::executor);
        while (this->status_ == stub_status::running)
        {
            std::string buffers_to_send = co_await sender_channel_.async_receive(net::use_awaitable);
            sender_timer.expires_after(500ms);
            co_await (net::async_write(method_channel_, net::buffer(buffers_to_send), net::use_awaitable) || sender_timer.async_wait(net::use_awaitable));
            spdlog::debug("{} sender_loop send size {}", id(), buffers_to_send.size());
        }
    }

    template<detail::async_stream MethodChannel>
    net::awaitable<void> stream_stub<MethodChannel>::receiver_loop()
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        uint64_t payload_size = 0;
        std::vector<uint8_t> payload(1500);

        while (this->status_ == stub_status::running)
        {
            co_await net::async_read(method_channel_, net::buffer(&payload_size, sizeof(payload_size)), net::use_awaitable);
            size_t size_read = co_await net::async_read(method_channel_, net::buffer(payload, payload_size), net::use_awaitable);
            spdlog::debug("{} receiver_loop received size {}", id(), size_read);
            // TODO size_read == payload_size

            uint64_t command_id;
            std::bitset<64> flags;
            rpc::Cookie cookie;
            auto message_payload = unpack(net::buffer(payload, payload_size), command_id, flags, cookie);

            if (std::bitset<64>(flags).test(detail::flag_is_request))
            {
                co_await dispatch_request(sender_channel_, command_id, std::move(cookie), std::move(message_payload));
            }
            else
            {
                co_await dispatch_response(command_id, std::move(cookie), message_payload);
            }
        }
    }

    template<detail::async_stream MethodChannel>
    template<detail::method_message Message>
    net::awaitable<detail::response_t<Message>> stream_stub<MethodChannel>::async_call(const detail::request_t<Message> &request, detail::duration_t timeout)
    {
        using namespace boost::asio::experimental::awaitable_operators;
        net::steady_timer timeout_timer(co_await net::this_coro::executor);
        timeout_timer.expires_after(timeout);
        std::variant<detail::response_t<Message>, std::monostate> result = co_await (do_async_call<Message>(sender_channel_, request) ||
                                                                                     timeout_timer.async_wait(net::use_awaitable));
        detail::response_t<Message> *message = std::get_if<detail::response_t<Message>>(&result);
        if (message != nullptr)
        {
            co_return std::move(*message);
        }
        throw detail::system_error::call_timeout;
    }
}// namespace acc_engineer::rpc

#endif//ACC_ENGINEER_SERVER_RPC_STUB_H
