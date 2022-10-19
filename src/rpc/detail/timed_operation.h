#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_CONDITIONAL_AWAITABLE_OPERATOR_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_CONDITIONAL_AWAITABLE_OPERATOR_H

#include <optional>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/experimental/cancellation_condition.hpp>
#include <boost/asio/experimental/parallel_group.hpp>

#include "error_code.h"

namespace acc_engineer::rpc::detail
{
    namespace net = boost::asio;
    namespace sys = boost::system;

    template<typename T, typename Executor>
    net::awaitable<T, Executor> awaitable_wrap(net::awaitable<T, Executor> a, typename std::enable_if<std::is_constructible<T>::value, int>::type * = 0)
    {
        return a;
    }

    template<typename T, typename Executor>
    net::awaitable<std::optional<T>, Executor>
    awaitable_wrap(net::awaitable<T, Executor> a, typename std::enable_if<!std::is_constructible<T>::value, int>::type * = 0)
    {
        co_return std::optional<T>(co_await std::move(a));
    }

    template<typename T>
    T &awaitable_unwrap(typename std::conditional<true, T, void>::type &r, typename std::enable_if<std::is_constructible<T>::value, int>::type * = 0)
    {
        return r;
    }

    template<typename T>
    T &awaitable_unwrap(
            std::optional<typename std::conditional<true, T, void>::type> &r,
            typename std::enable_if<!std::is_constructible<T>::value, int>::type * = 0)
    {
        return *r;
    }

    template<typename Executor, typename Timer>
    net::awaitable<void> timed_operation(Timer &timer, typename Timer::duration duration, net::awaitable<void, Executor> operation)
    {
        auto time_waiter = [&timer, duration]() -> net::awaitable<void>
        {
            timer.expires_after(duration);
            co_await timer.async_wait(net::use_awaitable);
            co_return;
        };

        auto ex = co_await net::this_coro::executor;
        auto parallel_group = net::experimental::make_parallel_group(
                co_spawn(ex, std::move(operation), net::deferred),
                co_spawn(ex, std::move(time_waiter), net::deferred));

        auto[completion_order, ex0, ex1] = co_await parallel_group.async_wait(net::experimental::wait_for_one(), net::deferred);

        if (completion_order[0] == 0)
        {
            if (ex0 != nullptr)
            {
                std::rethrow_exception(ex0);
            }

            co_return;
        }
        else
        {
            throw sys::system_error(system_error::operation_timeout);
        }
    }

    template<typename Executor, typename T, typename Timer>
    net::awaitable<T> timed_operation(Timer &timer, typename Timer::duration duration, net::awaitable<T, Executor> operation)
    {
        auto time_waiter = [&timer, duration]() -> net::awaitable<void>
        {
            timer.expires_after(duration);
            co_await timer.async_wait(net::use_awaitable);
            co_return;
        };

        auto ex = co_await net::this_coro::executor;
        auto parallel_group = net::experimental::make_parallel_group(
                co_spawn(ex, detail::awaitable_wrap<T>(std::move(operation)), net::deferred),
                co_spawn(ex, std::move(time_waiter), net::deferred));

        auto[completion_order, ex0, r0, ex1] = co_await parallel_group.async_wait(net::experimental::wait_for_one(), net::deferred);

        if (completion_order[0] == 0)
        {
            if (ex0 != nullptr)
            {
                std::rethrow_exception(ex0);
            }

            co_return std::move(detail::awaitable_unwrap<T>(r0));
        }
        else
        {
            throw sys::system_error(system_error::operation_timeout);
        }
    }
}

#endif //ACC_ENGINEER_SERVER_RPC_DETAIL_CONDITIONAL_AWAITABLE_OPERATOR_H
