#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_BATCH_TASK_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_BATCH_TASK_H

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

template<typename T>
using wrap_t = std::conditional_t<std::is_default_constructible_v<T>, T, std::optional<T>>;

template<typename T, typename Executor>
net::awaitable<T, Executor> awaitable_wrap(net::awaitable<T, Executor> a, std::enable_if_t<std::is_constructible_v<T>, int> * = nullptr)
{
    return a;
}

template<typename T, typename Executor>
net::awaitable<wrap_t<T>, Executor> awaitable_wrap(net::awaitable<T, Executor> a, std::enable_if_t<!std::is_constructible_v<T>, int> * = nullptr)
{
    co_return std::optional<T>(co_await std::move(a));
}

template<typename T>
T &unwrap(T &v, std::enable_if_t<std::is_default_constructible_v<T>, int> * = nullptr)
{
    return v;
}

template<typename T>
T &unwrap(std::optional<T> &v, std::enable_if_t<!std::is_default_constructible_v<T>, int> * = nullptr)
{
    return *v;
}

template<typename T, typename Executor = net::any_io_executor>
class batch_task : public boost::noncopyable
{
public:
    batch_task(Executor &executor);

public:
    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<T, Executor1> task);

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>, std::vector<T>>> async_wait();

    void cancel();

private:
    std::vector<std::unique_ptr<net::cancellation_signal>> cancellation_signals_;
    std::atomic<uint64_t> pending_tasks_;
    net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr, wrap_t<T>)> task_result_channel_;
};

template<typename Executor>
class batch_task<void, Executor> : public boost::noncopyable
{
public:
    batch_task(Executor &executor);

public:
    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<void, Executor1> task);

    net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>>> async_wait();

    void cancel();

private:
    std::vector<std::unique_ptr<net::cancellation_signal>> cancellation_signals_;
    std::atomic<uint64_t> pending_tasks_;
    std::vector<uint64_t> completion_order_;
    net::experimental::channel<Executor, void(sys::error_code, uint64_t, std::exception_ptr)> task_result_channel_;
};

template<typename T, typename Executor>
batch_task<T, Executor>::batch_task(Executor &executor)
    : pending_tasks_(0)
    , task_result_channel_(executor)
{}

template<typename Executor>
batch_task<void, Executor>::batch_task(Executor &executor)
    : pending_tasks_(0)
    , task_result_channel_(executor)
{}

template<typename T, typename Executor>
template<typename Executor1>
net::awaitable<void> batch_task<T, Executor>::add(net::awaitable<T, Executor1> task)
{
    auto executor = co_await net::this_coro::executor;
    auto order = pending_tasks_++;
    auto &cancellation_signal = cancellation_signals_.emplace_back(new net::cancellation_signal);

    net::co_spawn(
        executor, awaitable_wrap(std::move(task)), net::bind_cancellation_slot(cancellation_signal->slot(), [this, order](std::exception_ptr exception_ptr, wrap_t<T> value) {
            task_result_channel_.async_send({}, order, exception_ptr, std::move(value), [](sys::error_code) {});
        }));
}

template<typename Executor>
template<typename Executor1>
net::awaitable<void> batch_task<void, Executor>::add(net::awaitable<void, Executor1> task)
{
    auto executor = co_await net::this_coro::executor;
    auto order = pending_tasks_++;
    auto &cancellation_signal = cancellation_signals_.emplace_back(new net::cancellation_signal);

    net::co_spawn(executor, std::move(task), net::bind_cancellation_slot(cancellation_signal->slot(), [this, order](std::exception_ptr exception_ptr) {
        task_result_channel_.async_send({}, order, exception_ptr, [](sys::error_code) {});
    }));
}

template<typename T, typename Executor>
net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>, std::vector<T>>> batch_task<T, Executor>::async_wait()
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

template<typename Executor>
net::awaitable<std::tuple<std::vector<uint64_t>, std::vector<std::exception_ptr>>> batch_task<void, Executor>::async_wait()
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

template<typename T, typename Executor>
void batch_task<T, Executor>::cancel()
{
    for (auto &cancellation_signal : cancellation_signals_)
    {
        cancellation_signal->emit(net::cancellation_type::all);
    }
}

template<typename Executor>
void batch_task<void, Executor>::cancel()
{
    for (auto &cancellation_signal : cancellation_signals_)
    {
        cancellation_signal->emit(net::cancellation_type::all);
    }
}
} // namespace acc_engineer::rpc::detail

#endif