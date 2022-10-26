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

template<typename Executor>
class batch_task : public boost::noncopyable
{
public:
    batch_task(Executor &executor);

public:
    template<typename T, typename Executor1>
    net::awaitable<void> add(net::awaitable<T, Executor1> task, T &result);

    template<typename Executor1>
    net::awaitable<void> add(net::awaitable<void, Executor1> task);

    net::awaitable<void> async_wait();

    void cancel();

private:
    std::vector<std::unique_ptr<net::cancellation_signal>> cancellation_signals_;
    std::atomic<uint64_t> pending_tasks_;
    net::experimental::channel<Executor, void(sys::error_code, std::exception_ptr)> task_result_channel_;
};

template<typename Executor>
batch_task<Executor>::batch_task(Executor &executor)
    : pending_tasks_(0)
    , task_result_channel_(executor)
{}

template<typename Executor>
template<typename T, typename Executor1>
net::awaitable<void> batch_task<Executor>::add(net::awaitable<T, Executor1> task, T &result)
{
    auto executor = co_await net::this_coro::executor;
    pending_tasks_++;
    auto &cancellation_signal = cancellation_signals_.emplace_back(new net::cancellation_signal);

    net::co_spawn(executor, std::move(task), net::bind_cancellation_slot(cancellation_signal->slot(), [this, &result](std::exception_ptr exception_ptr, T value) {
        if (exception_ptr == nullptr)
        {
            result = std::move(value);
        }
        task_result_channel_.async_send({}, exception_ptr, [](sys::error_code) {});
    }));
}

template<typename Executor>
template<typename Executor1>
net::awaitable<void> batch_task<Executor>::add(net::awaitable<void, Executor1> task)
{
    auto executor = co_await net::this_coro::executor;
    pending_tasks_++;
    auto &cancellation_signal = cancellation_signals_.emplace_back(new net::cancellation_signal);

    net::co_spawn(executor, std::move(task), net::bind_cancellation_slot(cancellation_signal->slot(), [this](std::exception_ptr exception_ptr) {
        task_result_channel_.async_send({}, exception_ptr, [](sys::error_code) {});
    }));
}

template<typename Executor>
net::awaitable<void> batch_task<Executor>::async_wait()
{
    BOOST_SCOPE_EXIT_ALL(&)
    {
        cancellation_signals_.clear();
    };

    while (pending_tasks_ > 0)
    {
        std::exception_ptr exception = co_await task_result_channel_.async_receive(net::use_awaitable);
        if (exception != nullptr)
        {
            std::rethrow_exception(exception);
        }

        pending_tasks_--;
    }
}

template<typename Executor>
void batch_task<Executor>::cancel()
{
    for (auto &cancellation_signal : cancellation_signals_)
    {
        cancellation_signal->emit(net::cancellation_type::all);
    }
}

} // namespace acc_engineer::rpc::detail

#endif