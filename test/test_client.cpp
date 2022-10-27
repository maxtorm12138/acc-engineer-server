#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include "rpc/stub.h"

#include "service/config.h"

#include "proto/service.pb.h"

namespace net = boost::asio;
namespace rpc = acc_engineer::rpc;

net::awaitable<void> tcp_run(acc_engineer::config &config)
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::socket tcp_socket(executor);
    co_await tcp_socket.async_connect({config.address(), config.port()}, net::use_awaitable);

    auto tcp_stub = rpc::tcp_stub::create(std::move(tcp_socket));
}

net::awaitable<void> co_main(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;
    auto config = acc_engineer::config::from_command_line(argc, argv);

    net::ip::udp::socket udp_socket(executor);
    co_await udp_socket.async_connect({config.address(), config.port()}, net::use_awaitable);
}

int main(int argc, char *argv[])
{
    net::io_context io_context;
    net::co_spawn(io_context, co_main(argc, argv), [](std::exception_ptr exception_ptr) {
        if (exception_ptr)
        {
            std::rethrow_exception(exception_ptr);
        }
    });

    io_context.run();
}
