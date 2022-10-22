
#include <boost/asio.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/cancellation_condition.hpp>
#include <spdlog/spdlog.h>
#include <boost/system/error_code.hpp>

namespace net = boost::asio;
namespace sys = boost::system;

template<size_t N>
net::awaitable<void> job()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 1000;
    spdlog::error("job {} started pause {} ms", N, random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));

    sys::error_code ec;
    co_await timer.async_wait(net::redirect_error(net::use_awaitable, ec));
    if (ec)
    {
        spdlog::error("job {} error {}", N, ec.message());
        co_return;
    }

    spdlog::error("job {} ended", N);
}

template<size_t... N>
net::awaitable<void> runner(std::index_sequence<N...>)
{
    auto executor = co_await net::this_coro::executor;
    auto run = net::experimental::make_parallel_group(net::co_spawn(executor, job<N>(), net::deferred)...);
    co_await run.async_wait(net::experimental::wait_for_all(), net::deferred);
}

net::awaitable<void> random_stopper()
{
    using namespace std::chrono_literals;
    net::steady_timer timer(co_await net::this_coro::executor);
    auto random_ms = (rand() % 10000) + 1000;
    spdlog::error("stopper wait {} ms", random_ms);
    timer.expires_after(std::chrono::milliseconds(random_ms));
    co_await timer.async_wait(net::use_awaitable);
    spdlog::error("stopper triggered", random_ms);
}

net::awaitable<void> co_main()
{
    auto executor = co_await net::this_coro::executor;
    auto run = net::experimental::make_parallel_group(
        net::co_spawn(executor, runner(std::make_index_sequence<100>()), net::deferred), net::co_spawn(executor, random_stopper(), net::deferred));
    auto [order, exception_1, exception_2] = co_await run.async_wait(net::experimental::wait_for_one(), net::deferred);
    if (order[0] == 1)
    {
        spdlog::error("stopper triggered");
    }
    else {}
}

int main(int argc, char *argv)
{
    srand(time(nullptr));
    net::io_context context;
    net::co_spawn(context, co_main(), net::detached);
    context.run();
}