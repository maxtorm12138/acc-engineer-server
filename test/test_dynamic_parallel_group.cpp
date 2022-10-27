
#include <boost/asio.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/cancellation_condition.hpp>
#include <spdlog/spdlog.h>
#include <boost/system/error_code.hpp>

#include "rpc/batch_task.h"

namespace net = boost::asio;
namespace sys = boost::system;
namespace rpc = acc_engineer::rpc;

template<size_t N>
net::awaitable<void> void_job()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 1000;
    spdlog::info("void_job {} started pause {} ms", N, random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));

    sys::error_code ec;
    co_await timer.async_wait(net::redirect_error(net::use_awaitable, ec));
    if (ec)
    {
        spdlog::info("void_job {} error {}", N, ec.message());
    }

    spdlog::info("void_job {} ended", N);
}

template<size_t N>
net::awaitable<int> value_job()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 1000;
    spdlog::info("value_job {} started pause {} ms", N, random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));

    sys::error_code ec;
    co_await timer.async_wait(net::redirect_error(net::use_awaitable, ec));
    if (ec)
    {
        spdlog::info("value_job {} error {}", N, ec.message());
    }

    spdlog::info("value_job {} ended", N);
    co_return random_ms;
}

template<size_t N>
net::awaitable<void> cancel_job()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 100000;
    spdlog::info("long_time_job {} started pause {} ms", N, random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));

    co_await timer.async_wait(net::use_awaitable);

    spdlog::info("cancel_job {} ended", N);
}

net::awaitable<void> cancel_job_canceller(rpc::batch_task<void> &task)
{
    auto executor = co_await net::this_coro::executor;
    using namespace std::chrono_literals;
    net::steady_timer timer(executor);
    timer.expires_after(5s);
    spdlog::info("cancel_job_canceller started pause 5s");
    co_await timer.async_wait(net::use_awaitable);
    task.cancel();
    spdlog::info("cancel_job_canceller ended");
}

net::awaitable<void> run_void_job()
{
    auto executor = co_await net::this_coro::executor;
    rpc::batch_task<void> batch_task(executor);
    co_await batch_task.add(void_job<0>());
    co_await batch_task.add(void_job<1>());
    co_await batch_task.add(void_job<2>());
    co_await batch_task.add(void_job<3>());

    auto [order, exceptions] = co_await batch_task.async_wait();
    spdlog::info("void job end");
}

net::awaitable<void> run_cancel_job()
{
    auto executor = co_await net::this_coro::executor;
    rpc::batch_task<void> batch_task(executor);
    co_await batch_task.add(cancel_job<0>());
    co_await batch_task.add(cancel_job<1>());
    co_await batch_task.add(cancel_job<2>());
    co_await batch_task.add(cancel_job<3>());

    net::co_spawn(executor, cancel_job_canceller(batch_task), net::detached);
    auto [order, exceptions] = co_await batch_task.async_wait();
    for (int i = 0; i < order.size(); i++)
    {
        try
        {
            if (exceptions[i] != nullptr)
            {
                std::rethrow_exception(exceptions[i]);
            }
            spdlog::info("cancel_job {} no exception", order[i]);
        }
        catch (sys::system_error &ex)
        {
            spdlog::info("cancel_job {} system_error {}", order[i], ex.what());
        }
    }

    spdlog::info("cancel job end");
}

net::awaitable<void> run_value_job()
{
    auto executor = co_await net::this_coro::executor;
    rpc::batch_task<int> batch_task(executor);

    co_await batch_task.add(value_job<0>());
    co_await batch_task.add(value_job<1>());
    co_await batch_task.add(value_job<2>());
    co_await batch_task.add(value_job<3>());

    auto [order, exceptions, values] = co_await batch_task.async_wait();

    std::string value_str = "[";
    for (auto value : values)
    {
        value_str += std::to_string(value);
        value_str += ", ";
    }

    value_str += "]";
    spdlog::info("value_job ended, values: {}", value_str);
}

net::awaitable<void> co_main()
{
    co_await run_void_job();
    co_await run_value_job();
    co_await run_cancel_job();
}

int main(int argc, char *argv[])
{
    srand(static_cast<unsigned>(time(nullptr)));
    net::io_context context;
    net::co_spawn(context, co_main(), net::detached);
    context.run();
}