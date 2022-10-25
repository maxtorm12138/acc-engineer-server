#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_BATCH_TASK_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_BATCH_TASK_H

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/noncopyable.hpp>

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;
namespace sys = boost::asio;

template<typename T>
class batch_task : public boost::noncopyable
{
public:
    struct result
    {
        std::exception_ptr exception;
        T value;
    };

public:
    net::awaitable<void> add(net::awaitable<T> task);

private:
    uint64_t task_id_;
};

template<typename T>
net::awaitable<void> batch_task<T>::add(net::awaitable<T> task)
{
    auto executor = co_await net::this_coro::executor;
    auto async_task = net::co_spawn(executor, std::move(task), net::deferred);
}

} // namespace acc_engineer::rpc::detail

#endif