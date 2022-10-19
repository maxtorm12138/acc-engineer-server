#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H

#include "stub_base.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/deferred.hpp>

#include "error_code.h"

namespace acc_engineer::rpc::detail {
template<async_stream MethodChannel>
class stream_stub : public stub_base
{
public:
    explicit stream_stub(MethodChannel method_channel, const method_group &method_group = method_group::empty_method_group());

    net::awaitable<void> run();

    template<method_message Message>
    net::awaitable<response_t<Message>> async_call(const request_t<Message> &request);

private:
    net::awaitable<void> sender_loop();

    net::awaitable<void> receiver_loop();

    MethodChannel method_channel_;
    sender_channel_t sender_channel_;
};

template<async_stream MethodChannel>
stream_stub<MethodChannel>::stream_stub(MethodChannel method_channel, const method_group &method_group)
    : stub_base(method_group)
    , method_channel_(std::move(method_channel))
    , sender_channel_(method_channel_.get_executor())
{}

template<async_stream MethodChannel>
net::awaitable<void> stream_stub<MethodChannel>::run()
{
    using net::experimental::make_parallel_group;

    auto executor = co_await net::this_coro::executor;

    auto deferred_sender_loop = net::co_spawn(executor, sender_loop(), net::deferred);
    auto deferred_receiver_loop = net::co_spawn(executor, receiver_loop(), net::deferred);

    status_ = stub_status::running;
    auto parallel_group = make_parallel_group(std::move(deferred_receiver_loop), std::move(deferred_sender_loop));
    auto &&[order, ex_receiver, ex_sender] = co_await parallel_group.async_wait(net::experimental::wait_for_one(), net::deferred);
    spdlog::info("{} run stopped", id());

    if (order[0] == 0)
    {
        if (ex_receiver != nullptr)
        {
            std::rethrow_exception(ex_receiver);
        }
    }
    else
    {
        if (ex_sender != nullptr)
        {
            std::rethrow_exception(ex_sender);
        }
    }
}

template<async_stream MethodChannel>
net::awaitable<void> stream_stub<MethodChannel>::sender_loop()
{
    using namespace std::chrono_literals;
    using namespace boost::asio::experimental::awaitable_operators;
    using detail::stub_status;

    net::steady_timer sender_timer(co_await net::this_coro::executor);
    while (status()  == stub_status::running)
    {
        std::string buffers_to_send = co_await sender_channel_.async_receive(net::use_awaitable);
        sender_timer.expires_after(500ms);
        co_await (net::async_write(method_channel_, net::buffer(buffers_to_send), net::use_awaitable) || sender_timer.async_wait(net::use_awaitable));
        spdlog::debug("{} sender_loop send size {}", id(), buffers_to_send.size());
    }
}

template<async_stream MethodChannel>
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

        const size_t size_read = co_await net::async_read(method_channel_, net::buffer(payload.data() + sizeof(payload_size), payload_size), net::use_awaitable);

        if (size_read != payload_size)
        {
            throw sys::system_error(system_error::data_corrupted);
        }

        memcpy(payload.data(), &payload_size, sizeof(payload_size));
        spdlog::debug("{} receiver_loop received size {}", id(), sizeof(payload_size) + size_read);

        co_await dispatch(sender_channel_, net::buffer(payload, sizeof(payload_size) + size_read));
    }
}

template<async_stream MethodChannel>
template<method_message Message>
net::awaitable<response_t<Message>> stream_stub<MethodChannel>::async_call(const request_t<Message> &request)
{
    return do_async_call<Message>(sender_channel_, request);
}
} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H
