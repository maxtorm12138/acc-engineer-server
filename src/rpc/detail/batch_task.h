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
    std::vector<std::unique_ptr<net::cancellation_signal>> cancellation_signals_;
    std::atomic<uint64_t> pending_tasks_{0};
};

template<typename T, typename Executor = net::any_io_executor>
class batch_task : public batch_task_base
{
public:
    batch_task(Executor &executor)
        : task_result_channel_(executor){};

public:
    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<T, Executor1> task)
    {
        auto executor = co_await net::this_coro::executor;
        auto order = pending_tasks_++;
        auto &cancellation_signal = cancellation_signals_.emplace_back(new net::cancellation_signal);

        net::co_spawn(
            executor, wrap(std::move(task)), net::bind_cancellation_slot(cancellation_signal->slot(), [this, order](std::exception_ptr exception_ptr, wrap_type<T> value) {
                task_result_channel_.async_send({}, order, exception_ptr, std::move(value), [](sys::error_code) {});
            }));
    }

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>, std::vector<T>>> async_wait()
    {
        std::vector<uint64_t> completion_order;
        std::vector<std::exception_ptr> completion_exceptions;
        std::vector<T> completion_values;

        BOOST_SCOPE_EXIT_ALL(&)
        {
            cancellation_signals_.clear();
        };

        while (pending_tasks_)
        {
            auto [order, exception, value] = co_await task_result_channel_.async_receive(net::use_awaitable);

            completion_order.emplace_back(order);
            completion_exceptions.emplace_back(exception);
            completion_values.emplace_back(std::move(unwrap<T>(value)));

            pending_tasks_--;
        }

        co_return std::make_tuple(std::move(completion_order), std::move(completion_exceptions), std::move(completion_values));
    }

private:
    net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr, wrap_type<T>)> task_result_channel_;
};

template<typename Executor>
class batch_task<void, Executor> : public batch_task_base
{
public:
    batch_task(Executor &executor)
        : task_result_channel_(executor)
    {}

public:
    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<void, Executor1> task)
    {
        auto executor = co_await net::this_coro::executor;
        auto order = pending_tasks_++;
        auto &cancellation_signal = cancellation_signals_.emplace_back(new net::cancellation_signal);

        net::co_spawn(executor, std::move(task), net::bind_cancellation_slot(cancellation_signal->slot(), [this, order](std::exception_ptr exception_ptr) {
            task_result_channel_.async_send({}, order, exception_ptr, [](sys::error_code) {});
        }));
    }

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>>> async_wait()
    {
        std::vector<uint64_t> completion_order;
        std::vector<std::exception_ptr> completion_exceptions;

        BOOST_SCOPE_EXIT_ALL(&)
        {
            cancellation_signals_.clear();
        };

        while (pending_tasks_)
        {
            auto [order, exception] = co_await task_result_channel_.async_receive(net::use_awaitable);

            completion_order.emplace_back(order);
            completion_exceptions.emplace_back(exception);

            pending_tasks_--;
        }

        co_return std::make_tuple(std::move(completion_order), std::move(completion_exceptions));
    }

private:
    net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr)> task_result_channel_;
};

} // namespace acc_engineer::rpc::detail

#endif