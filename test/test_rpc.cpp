#include <boost/asio.hpp>
#include <iostream>
#include <list>
#include <string>

#include "proto/service.pb.h"
#include "rpc/stub.h"
#include "rpc/types.h"

namespace net = boost::asio;
namespace proto = acc_engineer;
namespace rpc = acc_engineer::rpc;

using namespace std::string_literals;

using tcp_stub_type = rpc::stream_stub<net::ip::tcp::socket>;
using udp_stub_type = rpc::datagram_stub<net::ip::udp::socket>;

net::awaitable<rpc::response_t<proto::Echo>> echo(const proto::Echo::Request &request)
{
    proto::Echo::Response response;
    response.set_message(request.message());
    co_return std::move(response);
}

std::list<tcp_stub_type> tcp_stubs;
std::list<udp_stub_type> udp_stubs;

net::awaitable<void> server_tcp(int argc, char *argv[])
{
    rpc::method_group method_group;
    method_group.implement<proto::Echo>(0, echo);

    auto executor = co_await net::this_coro::executor;

    net::ip::tcp::acceptor acceptor(executor, net::ip::tcp::endpoint{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))});

    for (;;)
    {
        auto socket = co_await acceptor.async_accept(net::use_awaitable);
        auto &stub = tcp_stubs.emplace_back(std::move(socket), method_group);
        co_await stub.run();
    }
}

net::awaitable<void> server_udp(int argc, char *argv[])
{
    try
    {
        rpc::method_group method_group;
        method_group.implement<proto::Echo>(0, echo);

        auto executor = co_await net::this_coro::executor;

        net::ip::udp::socket acceptor(executor, {net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))});
        acceptor.set_option(net::socket_base::reuse_address(true));

        std::array<uint8_t, 1500> buffer{};
        for (;;)
        {
            net::ip::udp::endpoint remote;
            auto size_read = co_await acceptor.async_receive_from(net::buffer(buffer), remote, net::use_awaitable);

            net::ip::udp::socket socket(executor, {net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))});
            socket.connect(remote);
            socket.set_option(net::socket_base::reuse_address(true));

            auto &stub = udp_stubs.emplace_back(std::move(socket), method_group);
            co_await stub.run(net::buffer(buffer, size_read));
        }
    }
    catch (boost::system::system_error &e)
    {
        std::cerr << e.what() << std::endl;
    }
}


net::awaitable<void> client_tcp(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::socket socket(executor);

    co_await socket.async_connect(
            net::ip::tcp::endpoint{net::ip::make_address(argv[3]), static_cast<net::ip::port_type>(std::stoi(argv[4]))},
            net::use_awaitable);

    tcp_stub_type stub(std::move(socket));
    co_await stub.run();

    for (;;)
    {
        proto::Echo::Request request;
        std::getline(std::cin, *request.mutable_message());

        auto result = co_await stub.async_call<acc_engineer::Echo>(request);
        std::cerr << "Response: " << result.ShortDebugString() << std::endl;
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
    co_await stub.run();

    for (;;)
    {
        proto::Echo::Request request;
        std::getline(std::cin, *request.mutable_message());

        auto result = co_await stub.async_call<acc_engineer::Echo>(request);
        std::cerr << "Response: " << result.ShortDebugString() << std::endl;
    }
}


int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    net::io_context io_context;

    std::string argv1(argv[1]);

    if (argv[1] == "server"s)
    {
        if (argv[2] == "udp"s)
        {
            net::co_spawn(io_context, server_udp(argc, argv), net::detached);
        }
        else
        {
            net::co_spawn(io_context, server_tcp(argc, argv), net::detached);
        }
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


    io_context.run();
}