#include <string>
#include <list>
#include <iostream>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/connect.hpp>

#include "rpc/stub.h"
#include "proto/service.pb.h"

namespace net = boost::asio;
namespace proto = acc_engineer;
namespace rpc = acc_engineer::rpc;

using namespace std::string_literals;

using stub_type = rpc::stub<net::ip::tcp::socket>;

net::awaitable<rpc::response_t<proto::Echo>> echo(const proto::Echo::Request& request)
{
	proto::Echo::Response response;
	response.set_message(request.message());
	co_return std::move(response);
}

std::list<stub_type> stubs;

net::awaitable<void> server(int argc, char* argv[])
{
	rpc::method_group method_group;
    method_group.implement<proto::Echo>(0, echo);

	auto executor = co_await net::this_coro::executor;

	net::ip::tcp::acceptor acceptor(executor, net::ip::tcp::endpoint{ net::ip::make_address(argv[2]), static_cast<net::ip::port_type>(std::stoi(argv[3])) });

	for (;;)
	{
        auto socket = co_await acceptor.async_accept(net::use_awaitable);
        auto &stub = stubs.emplace_back(std::move(socket), method_group);
        co_await stub.run();
    }
}


net::awaitable<void> client(int argc, char* argv[])
{
	auto executor = co_await net::this_coro::executor;
	net::ip::tcp::socket socket(executor);

	co_await socket.async_connect(
		net::ip::tcp::endpoint{ net::ip::make_address(argv[2]), static_cast<net::ip::port_type>(std::stoi(argv[3])) },
		net::use_awaitable);

	rpc::stub stub(std::move(socket));
    co_await stub.run();

    for (;;)
    {
        proto::Echo::Request request;
        std::getline(std::cin, *request.mutable_message());

        auto result = co_await stub.async_call<acc_engineer::Echo>(request);
        std::cerr << "Response: " << result.value().ShortDebugString() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::debug);
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