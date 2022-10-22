#include <boost/asio.hpp>

#include <spdlog/spdlog.h>

#include "proto/service.pb.h"
#include "rpc/stub.h"

namespace net = boost::asio;
using namespace acc_engineer;

net::awaitable<void> client_udp(int argc, char *argv[]) {}
net::awaitable<void> client_tcp(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::socket socket(executor);
    net::ip::tcp::endpoint ep{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))};
    co_await socket.async_connect(ep, net::use_awaitable);

    rpc::tcp_stub stub(std::move(socket));
    net::co_spawn(executor, stub.run(), net::detached);

    for (;;)
    {
        try
        {
            using namespace std::chrono_literals;
            Echo::Request request;
            std::getline(std::cin, *request.mutable_message());

            auto result = co_await stub.async_call<Echo>(request);
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
    spdlog::set_level(spdlog::level::debug);
    net::io_context io_context;
    using namespace std::string_literals;
    if (argv[1] == "udp"s)
    {
        net::co_spawn(io_context, client_udp(argc, argv), net::detached);
    }
    else if (argv[1] == "tcp"s)
    {
        net::co_spawn(io_context, client_tcp(argc, argv), net::detached);
    }

    io_context.run();
    return 0;
}
/*
namespace net = boost::asio;
namespace proto = acc_engineer;
namespace rpc = acc_engineer::rpc;

using namespace std::string_literals;

using tcp_stub_type = rpc::stream_stub<net::ip::tcp::socket>;
using udp_stub_type = rpc::datagram_stub<net::ip::udp::socket>;

net::awaitable<void> client_tcp(int argc, char *argv[])
{
auto executor = co_await net::this_coro::executor;
net::ip::tcp::socket socket(executor);

co_await socket.async_connect(
        net::ip::tcp::endpoint{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))},
        net::use_awaitable);

tcp_stub_type stub(std::move(socket));
net::co_spawn(executor, stub.run(), net::detached);

for (;;)
{
    try
    {
        using namespace std::chrono_literals;
        proto::Echo::Request request;
        std::getline(std::cin, *request.mutable_message());

        auto result = co_await stub.async_call<acc_engineer::Echo>(request);
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

net::awaitable<void> client_udp(int argc, char *argv[])
{
auto executor = co_await net::this_coro::executor;
net::ip::udp::socket socket(executor);

co_await socket.async_connect(
        net::ip::udp::endpoint{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))},
        net::use_awaitable);

udp_stub_type stub(std::move(socket));
net::co_spawn(executor, stub.run(), net::detached);

for (;;)
{
    try
    {
        using namespace std::chrono_literals;
        proto::Echo::Request request;
        std::getline(std::cin, *request.mutable_message());

        auto result = co_await stub.async_call<acc_engineer::Echo>(request);
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
spdlog::set_level(spdlog::level::debug);
net::io_context io_context;

std::string argv1(argv[1]);

if (argv[1] == "server"s)
{
}
else if (argv[1] == "client"s)
{
    if (argv[2] == "udp"s)
    {
        net::co_spawn(io_context, client_udp(argc, argv), net::detached);
    }
    else
    {
        net::co_spawn(io_context, client_tcp(argc, argv), net::detached);
    }
}

try
{
    io_context.run();
}
catch (boost::system::system_error &e)
{
    std::cerr << "io_context System error: " << e.what() << std::endl;
}
catch (std::exception &e)
{
    std::cerr << "io_context exception: " << e.what() << std::endl;
}
}
*/