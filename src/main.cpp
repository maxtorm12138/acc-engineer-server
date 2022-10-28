#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>

#include "service/config.h"
#include "service/service.h"

namespace net = boost::asio;

net::awaitable<void> co_signal(std::shared_ptr<acc_engineer::service> service)
{
    auto executor = co_await net::this_coro::executor;
    net::signal_set signal_set(executor, SIGINT, SIGTERM);
    auto signal = co_await signal_set.async_wait(net::use_awaitable);
    SPDLOG_INFO("signal {}", signal);
    co_await service->stop();
}

net::awaitable<void> co_main(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;
    auto config = acc_engineer::config::from_command_line(argc, argv);
    auto service = std::make_shared<acc_engineer::service>(config);
    net::co_spawn(executor, co_signal(service), net::detached);
    co_await service->run();
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);

    net::io_context io_context;

    net::co_spawn(io_context, co_main(argc, argv), [](const std::exception_ptr &exception_ptr) {
        try
        {
            if (exception_ptr != nullptr)
            {
                std::rethrow_exception(exception_ptr);
            }
        }
        catch (const std::exception &ex)
        {
            SPDLOG_ERROR("co_main exception: {}", ex.what());
        }
        catch (...)
        {
            SPDLOG_CRITICAL("co_main unkwown exception");
        }
    });

    io_context.run();
    return 0;
}
