#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_BATCH_TASK_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_BATCH_TASK_H
#pragma once
// std
#include <optional>

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/scope_exit.hpp>
#include <boost/noncopyable.hpp>

// spdlog
#include <spdlog/spdlog.h>

#include "await_error_code.h"
#include "error_code.h"

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;
namespace sys = boost::system;

class batch_task_base : public boost::noncopyable
{
public:
    void cancel();

protected:
    template<typename T>
    using wrap_type = std::conditional_t<std::is_default_constructible_v<T>, T, std::optional<T>>;

    template<typename T, typename Executor>
    static net::awaitable<T, Executor> wrap(net::awaitable<T, Executor> a, std::enable_if_t<std::is_constructible_v<T>, int> * = nullptr)
    {
        return a;
    }

    template<typename T, typename Executor>
    static net::awaitable<wrap_type<T>, Executor> wrap(net::awaitable<T, Executor> a, std::enable_if_t<!std::is_constructible_v<T>, int> * = nullptr)
    {
        co_return std::optional<T>(co_await std::move(a));
    }

    template<typename T>
    static T &unwrap(T &v, std::enable_if_t<std::is_default_constructible_v<T>, int> * = nullptr)
    {
        return v;
    }

    template<typename T>
    static T &unwrap(std::optional<T> &v, std::enable_if_t<!std::is_default_constructible_v<T>, int> * = nullptr)
    {
        return *v;
    }

protected:
    std::vector<std::weak_ptr<net::cancellation_signal>> cancellation_signals_;
    std::atomic<uint64_t> pending_tasks_{0};
};

template<typename T, typename Executor = net::any_io_executor>
class batch_task : public batch_task_base
{
public:
    batch_task(Executor &executor)
        : task_result_channel_(std::make_shared<net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr, wrap_type<T>)>>(executor))
    {}

public:
    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<T, Executor1> task)
    {
        auto executor = co_await net::this_coro::executor;
        auto order = pending_tasks_++;

        auto cancellation_signal = std::make_shared<net::cancellation_signal>();
        cancellation_signals_.emplace_back(cancellation_signal);

        auto chan = task_result_channel_;

        net::co_spawn(executor, wrap(std::move(task)),
            net::bind_cancellation_slot(cancellation_signal->slot(), [chan, cancellation_signal, order](std::exception_ptr exception_ptr, wrap_type<T> value) {
                chan->async_send({}, order, exception_ptr, std::move(value), [](sys::error_code error_code) { SPDLOG_DEBUG("add error: {}", error_code.message()); });
            }));
    }

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>, std::vector<T>>> async_wait()
    {
        sys::error_code error_code;
        auto result = co_await async_wait(error_code);
        if (error_code)
        {
            throw sys::system_error(error_code);
        }

        co_return result;
    }

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>, std::vector<T>>> async_wait(sys::error_code &error_code)
    {
        auto calcelation = co_await net::this_coro::cancellation_state;
        std::vector<uint64_t> completion_order;
        std::vector<std::exception_ptr> completion_exceptions;
        std::vector<T> completion_values;

        BOOST_SCOPE_EXIT_ALL(&)
        {
            cancellation_signals_.clear();
        };

        while (pending_tasks_)
        {
            sys::error_code ec;
            auto [order, exception, value] = co_await task_result_channel_->async_receive(await_error_code(ec));
            if (ec)
            {
                cancel();
                error_code = system_error::operation_canceled;
                task_result_channel_ = nullptr;
                break;
            }

            completion_order.emplace_back(order);
            completion_exceptions.emplace_back(exception);
            completion_values.emplace_back(std::move(unwrap<T>(value)));

            pending_tasks_--;
        }

        co_return std::make_tuple(std::move(completion_order), std::move(completion_exceptions), std::move(completion_values));
    }

private:
    std::shared_ptr<net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr, wrap_type<T>)>> task_result_channel_;
};

template<typename Executor>
class batch_task<void, Executor> : public batch_task_base
{
public:
    batch_task(Executor &executor)
        : task_result_channel_(std::make_shared<net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr)>>(executor))
    {}

public:
    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<void, Executor1> task)
    {
        auto executor = co_await net::this_coro::executor;
        auto order = pending_tasks_++;

        auto cancellation_signal = std::make_shared<net::cancellation_signal>();
        cancellation_signals_.emplace_back(cancellation_signal);

        auto chan = task_result_channel_;

        net::co_spawn(executor, std::move(task), net::bind_cancellation_slot(cancellation_signal->slot(), [chan, cancellation_signal, order](std::exception_ptr exception_ptr) {
            chan->async_send({}, order, exception_ptr, [](sys::error_code error_code) { SPDLOG_DEBUG("add error: {}", error_code.message()); });
        }));
    }

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>>> async_wait()
    {
        sys::error_code error_code;
        auto result = co_await async_wait(error_code);
        if (error_code)
        {
            throw sys::system_error(error_code);
        }

        co_return result;
    }

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>>> async_wait(sys::error_code &error_code)
    {
        std::vector<uint64_t> completion_order;
        std::vector<std::exception_ptr> completion_exceptions;

        BOOST_SCOPE_EXIT_ALL(&)
        {
            cancellation_signals_.clear();
        };

        while (pending_tasks_)
        {
            sys::error_code ec;
            auto [order, exception] = co_await task_result_channel_->async_receive(await_error_code(ec));
            if (ec)
            {
                cancel();
                error_code = system_error::operation_canceled;
                task_result_channel_ = nullptr;
                break;
            }

            completion_order.emplace_back(order);
            completion_exceptions.emplace_back(exception);

            pending_tasks_--;
        }

        co_return std::make_tuple(std::move(completion_order), std::move(completion_exceptions));
    }

private:
    std::shared_ptr<net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr)>> task_result_channel_;
};

} // namespace acc_engineer::rpc::detail

#endif