#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H

#include "stub_base.h"

#include <boost/asio/detached.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include "error_code.h"

namespace acc_engineer::rpc::detail
{
    template<detail::async_stream MethodChannel>
    class stream_stub : public detail::stub_base
    {
    public:
        explicit stream_stub(MethodChannel method_channel, const method_group &method_group = method_group::empty_method_group());

        net::awaitable<void> run();

        template<detail::method_message Message>
        net::awaitable<response_t<Message>> async_call(const detail::request_t<Message> &request, detail::duration_t timeout = detail::duration_t::max());

    private:

        net::awaitable<void> sender_loop();

        net::awaitable<void> receiver_loop();

        MethodChannel method_channel_;
        sender_channel_t sender_channel_;
    };

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

        std::string payload(MAX_PAYLOAD_SIZE, '\0');

        while (this->status_ == stub_status::running)
        {
            uint64_t payload_size = 0;
            co_await net::async_read(method_channel_, net::buffer(&payload_size, sizeof(payload_size)), net::use_awaitable);

            size_t size_read = co_await net::async_read(
                    method_channel_,
                    net::buffer(payload.data() + sizeof(payload_size), payload.size() - payload_size),
                    net::use_awaitable);

            if (size_read != payload_size)
            {
                throw sys::system_error(system_error::data_corrupted);
            }

            memcpy(payload.data(), &payload_size, sizeof(payload_size));
            spdlog::debug("{} receiver_loop received size {}", id(), sizeof(payload_size) + size_read);

            co_await dispatch(sender_channel_, net::buffer(payload, sizeof(payload_size) + size_read));
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

        throw sys::system_error(detail::system_error::call_timeout);
    }

}

#endif //ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H
