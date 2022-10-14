#include "rpc.h"
#include "service.pb.h"
#include <string>
#include <iostream>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace net = boost::asio;
namespace sys = boost::system;
namespace proto = acc_engineer::proto;
namespace rpc = acc_engineer::rpc;

using namespace std::string_literals;

net::awaitable<proto::Echo::Response> echo(rpc::request_id_type request_id, const proto::Echo::Request &request)
{
    proto::Echo::Response response;
    response.set_message(request.message());
    co_return response;
}

net::awaitable<void> run_rpc_service(net::ip::tcp::socket socket)
{
    acc_engineer::rpc::server_service rpc_server_service(socket);
    co_await rpc_server_service.run();
}

net::awaitable<void> server(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;

    acc_engineer::rpc::server_service::register_method<proto::Echo>(0, echo);

    net::ip::tcp::acceptor acceptor(executor, net::ip::tcp::endpoint{net::ip::make_address(argv[2]), static_cast<net::ip::port_type>(std::stoi(argv[3]))});

    for (;;)
    {
        auto socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(executor, run_rpc_service(std::move(socket)), net::detached);
    }
}


net::awaitable<void> client(int argc, char *argv[])
{
    auto executor = co_await net::this_coro::executor;

    net::ip::tcp::socket socket(executor);
    co_await socket.async_connect(
            net::ip::tcp::endpoint{net::ip::make_address(argv[2]), static_cast<net::ip::port_type>(std::stoi(argv[3]))},
            net::use_awaitable);

    acc_engineer::rpc::client_service rpc_client_service(socket);
    co_await rpc_client_service.run();

    for (;;)
    {
        proto::Echo::Request request;
        std::getline(std::cin, *request.mutable_message());

        auto result = co_await rpc_client_service.async_call(request);

        std::cerr << "Response: " << result.value().ShortDebugString() << std::endl;
    }
}

int main(int argc, char *argv[])
{
    net::io_context io_context;

    if (argv[1] == "server"s)
    {
        net::co_spawn(io_context, server(argc, argv), net::detached);
    }
    else if (argv[1] == "client"s)
    {
        net::co_spawn(io_context, client(argc, argv), net::detached);
    }

    io_context.run();
}