#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>

#include "service/config.h"
#include "service/service.h"

namespace net = boost::asio;

net::awaitable<void> co_main(int argc, char *argv[])
{
    auto config = acc_engineer::config::from_command_line(argc, argv);
    acc_engineer::service service(config);
    co_await service.run();
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);

    net::io_context io_context;

    net::co_spawn(io_context, co_main(argc, argv), [](const std::exception_ptr &exception_ptr) {
        try
        {
            if (exception_ptr != nullptr)
            {
                std::rethrow_exception(exception_ptr);
            }
        }
        catch(const std::exception & ex)
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
