#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_STREAM_STUB_H

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include "stub_base.h"
#include "timed_operation.h"
#include "await_ec.h"
#include "error_code.h"

namespace acc_engineer::rpc::detail {
template<async_stream MethodChannel>
class stream_stub : public stub_base
{
public:
    explicit stream_stub(MethodChannel method_channel, const method_group &method_group = method_group::empty_method_group());

    net::awaitable<void> run();

    net::awaitable<void> stop();

    template<method_message Message>
    net::awaitable<response_t<Message>> async_call(const request_t<Message> &request);

private:
    net::awaitable<void> sender_loop();

    net::awaitable<void> receiver_loop();

    MethodChannel method_channel_;
    sender_channel_t sender_channel_;
    stopping_channel_t stopping_channel_;
};

template<async_stream MethodChannel>
stream_stub<MethodChannel>::stream_stub(MethodChannel method_channel, const method_group &method_group)
    : stub_base(method_group)
    , method_channel_(std::move(method_channel))
    , sender_channel_(method_channel_.get_executor())
    , stopping_channel_(method_channel_.get_executor())
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

    for (auto &&[trace_id, calling_channel] : calling_)
    {
        calling_channel->close();
    }

    auto previous_status = status_;
    status_ = stub_status::stopped;

    if (previous_status == stub_status::stopping)
    {
        spdlog::info("{} run stopped normally", id());
        co_await stopping_channel_.async_send({}, id(), net::use_awaitable);
    }
    else if (order[0] == 0)
    {
        if (ex_receiver != nullptr)
        {
            spdlog::warn("{} run receiver exception occurred", id());
            std::rethrow_exception(ex_receiver);
        }
    }
    else
    {
        if (ex_sender != nullptr)
        {
            spdlog::warn("{} run sender exception occurred", id());
            std::rethrow_exception(ex_sender);
        }
    }
}

template<async_stream MethodChannel>
net::awaitable<void> stream_stub<MethodChannel>::stop()
{
    if (status_ == stub_status::running)
    {
        status_ = stub_status::stopping;
        method_channel_.cancel();
        sender_channel_.cancel();
        co_await stopping_channel_.async_receive(net::use_awaitable);
    }
    co_return;
}

template<async_stream MethodChannel>
net::awaitable<void> stream_stub<MethodChannel>::sender_loop()
{
    try
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        net::steady_timer sender_timer(co_await net::this_coro::executor);
        while (status_ == stub_status::running)
        {
            sys::error_code ec;
            std::string buffers_to_send = co_await sender_channel_.async_receive(await_ec[ec]);
            if (ec)
            {
                if (ec.category() == net::experimental::error::channel_category)
                {
                    spdlog::info("{} sender_loop stopped, sender_channel: {}", id(), ec.message());
                    co_return;
                }
                throw sys::system_error(ec);
            }

            co_await timed_operation(sender_timer, 500ms, net::async_write(method_channel_, net::buffer(buffers_to_send), await_ec[ec]));
            if (ec)
            {
                if (ec == net::error::eof || ec == net::error::operation_aborted)
                {
                    spdlog::info("{} sender_loop stopped, method_channel: {}", id(), ec.message());
                    co_return;
                }
                throw sys::system_error(ec);
            }

            spdlog::debug("{} sender_loop send size {}", id(), buffers_to_send.size());
        }
    }
    catch (const sys::system_error &ex)
    {
        spdlog::warn("{} sender_loop system_error, what: {}", id(), ex.what());
        throw;
    }
    catch (const std::exception &ex)
    {
        spdlog::warn("{} sender_loop std::exception, what: {}", id(), ex.what());
        throw;
    }
}

template<async_stream MethodChannel>
net::awaitable<void> stream_stub<MethodChannel>::receiver_loop()
{
    try
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        std::string payload(MAX_PAYLOAD_SIZE, '\0');
        net::steady_timer receiver_timer(co_await net::this_coro::executor);

        while (status_ == stub_status::running)
        {
            sys::error_code ec;

            uint64_t payload_size = 0;
            co_await net::async_read(method_channel_, net::buffer(&payload_size, sizeof(payload_size)), await_ec[ec]);
            if (ec)
            {
                if (ec == net::error::eof || ec == net::error::operation_aborted)
                {
                    spdlog::info("{} receiver_loop stopped, method_channel: {}", id(), ec.message());
                    co_return;
                }
                throw sys::system_error(ec);
            }

            if (payload_size > MAX_PAYLOAD_SIZE)
            {
                throw sys::system_error(system_error::data_corrupted);
            }

            const size_t size_read =
                co_await timed_operation(receiver_timer, 500ms, net::async_read(method_channel_, net::buffer(payload.data() + sizeof(payload_size), payload_size), await_ec[ec]));

            if (ec)
            {
                if (ec == net::error::eof || ec == net::error::operation_aborted)
                {
                    spdlog::info("{} receiver_loop stopped, method_channel: {}", id(), ec.message());
                    co_return;
                }
                throw sys::system_error(ec);
            }

            if (size_read != payload_size)
            {
                throw sys::system_error(system_error::data_corrupted);
            }
            memcpy(payload.data(), &payload_size, sizeof(payload_size));

            spdlog::debug("{} receiver_loop received size {}", id(), sizeof(payload_size) + size_read);

            co_await dispatch(sender_channel_, net::buffer(payload, sizeof(payload_size) + size_read));
        }
    }
    catch (const sys::system_error &ex)
    {
        spdlog::warn("{} receiver_loop system_error, what: {}", id(), ex.what());
        throw;
    }
    catch (const std::exception &ex)
    {
        spdlog::warn("{} receiver_loop std::exception, what: {}", id(), ex.what());
        throw;
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
