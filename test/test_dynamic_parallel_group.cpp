
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
net::awaitable<void> job()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 1000;
    spdlog::info("job {} started pause {} ms", N, random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));

    sys::error_code ec;
    co_await timer.async_wait(net::redirect_error(net::use_awaitable, ec));
    if (ec)
    {
        spdlog::info("job {} error {}", N, ec.message());
    }

    spdlog::info("job {} ended", N);
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
net::awaitable<void> long_time_job()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 100000;
    spdlog::info("long_time_job {} started pause {} ms", N, random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));

    sys::error_code ec;
    co_await timer.async_wait(net::redirect_error(net::use_awaitable, ec));
    if (ec)
    {
        spdlog::info("long_time_job {} error {}", N, ec.message());
    }

    spdlog::info("long_time_job {} ended", N);
}

template<typename Executor>
net::awaitable<void> long_time_job_canceller(rpc::batch_task<Executor> &task)
{
    auto executor = co_await net::this_coro::executor;
    using namespace std::chrono_literals;
    net::steady_timer timer(executor);
    timer.expires_after(5s);
    spdlog::info("long_time_job_canceller started pause 5s");
    co_await timer.async_wait(net::use_awaitable);
    task.cancel();
    spdlog::info("long_time_job_canceller ended");
}

net::awaitable<void> co_main()
{
    auto executor = co_await net::this_coro::executor;
    rpc::batch_task batch_task(executor);
    co_await batch_task.add(job<0>());
    co_await batch_task.add(job<1>());
    co_await batch_task.add(job<2>());
    co_await batch_task.add(job<3>());

    co_await batch_task.async_wait();
    spdlog::info("job ended");

    std::vector<int> values(4);
    co_await batch_task.add(value_job<0>(), values[0]);
    co_await batch_task.add(value_job<1>(), values[1]);
    co_await batch_task.add(value_job<2>(), values[2]);
    co_await batch_task.add(value_job<3>(), values[3]);

    co_await batch_task.async_wait();
    spdlog::info("values[0]: {}", values[0]);
    spdlog::info("values[1]: {}", values[1]);
    spdlog::info("values[2]: {}", values[2]);
    spdlog::info("values[3]: {}", values[3]);
    spdlog::info("value_job ended");

    co_await batch_task.add(long_time_job<0>());
    co_await batch_task.add(long_time_job<1>());
    co_await batch_task.add(long_time_job<2>());
    co_await batch_task.add(long_time_job<3>());

    net::co_spawn(executor, long_time_job_canceller(batch_task), net::detached);
    co_await batch_task.async_wait();
    spdlog::info("long_time_job ended");
}

int main(int argc, char *argv[])
{
    srand(static_cast<unsigned>(time(nullptr)));
    net::io_context context;
    net::co_spawn(context, co_main(), net::detached);
    context.run();
}