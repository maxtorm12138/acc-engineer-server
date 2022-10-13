#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>

#include "config.h"
#include "service.h"

namespace net = boost::asio;

net::awaitable<void> co_main(int argc, char *argv[])
{
    auto config = acc_engineer::config::from_command_line(argc, argv);
    acc_engineer::service service(config);
}

int main(int argc, char *argv[])
{
    net::io_context io_context;

    net::co_spawn(io_context, co_main(argc, argv), [](const std::exception_ptr& exception_ptr)
    {
        if (exception_ptr != nullptr)
        {
            std::rethrow_exception(exception_ptr);
        }
    });

    io_context.run();
    return 0;
}
