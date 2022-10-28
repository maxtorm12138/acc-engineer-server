#include <boost/asio.hpp>

#include <spdlog/spdlog.h>

#include "proto/service.pb.h"
#include "rpc/stub.h"

namespace net = boost::asio;
using namespace acc_engineer;

net::awaitable<void> client_udp(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;
    net::ip::udp::socket socket(executor);
    net::ip::udp::endpoint ep{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))};
    co_await socket.async_connect(ep, net::use_awaitable);

    auto stub = rpc::udp_stub::create(std::move(socket));
    net::co_spawn(
        executor, [=]() -> net::awaitable<void> { co_await stub->run(); }, net::detached);

    for (;;)
    {
        try
        {
            using namespace std::chrono_literals;
            Echo::Request request;
            std::getline(std::cin, *request.mutable_message());

            auto result = co_await stub->async_call<Echo>(request);
            std::cerr << "Response: " << result.ShortDebugString() << std::endl;
        }
        catch (boost::system::system_error &e)
        {
            std::cerr << "System error: " << e.what() << std::endl;
        }
        catch (std::exception &e)
        {
            std::cerr << "exception: " << e.what() << std::endl;
        }
    }
}

net::awaitable<void> client_tcp(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::socket socket(executor);
    net::ip::tcp::endpoint ep{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))};
    co_await socket.async_connect(ep, net::use_awaitable);

    auto stub = rpc::tcp_stub::create(std::move(socket));
    net::co_spawn(
        executor, [=]() -> net::awaitable<void> { co_await stub->run(); }, net::detached);

    for (;;)
    {
        try
        {
            using namespace std::chrono_literals;
            Echo::Request request;
            std::getline(std::cin, *request.mutable_message());

            auto result = co_await stub->async_call<Echo>(request);
            std::cerr << "Response: " << result.ShortDebugString() << std::endl;
        }
        catch (boost::system::system_error &e)
        {
            std::cerr << "System error: " << e.what() << std::endl;
        }
        catch (std::exception &e)
        {
            std::cerr << "exception: " << e.what() << std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);
    net::io_context io_context;
    using namespace std::string_literals;
    if (argv[1] == "client"s)
    {
        if (argv[2] == "udp"s)
        {
            net::co_spawn(io_context, client_udp(argc, argv), net::detached);
        }
        else if (argv[2] == "tcp"s)
        {
            net::co_spawn(io_context, client_tcp(argc, argv), net::detached);
        }
    }

    io_context.run();
    return 0;
}
