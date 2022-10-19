#include "service.h"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include "proto/service.pb.h"

namespace acc_engineer
{
	service::service(config cfg)
		: config_(std::move(cfg))
	{
	}

	net::awaitable<void> service::run()
	{
		using namespace std::placeholders;
		running_ = true;
		method_group_.implement<Echo>(std::bind(&service::echo, this, _1));

		co_spawn(co_await net::this_coro::executor, udp_run(), net::detached);
		co_spawn(co_await net::this_coro::executor, tcp_run(), net::detached);
	}

	net::awaitable<void> service::tcp_run()
	{
		auto executor = co_await net::this_coro::executor;
		net::ip::tcp::acceptor acceptor(executor,
		                                net::ip::tcp::endpoint{
			                                config_.address(), config_.port()
		                                });

		while (running_)
		{
			auto socket = co_await acceptor.async_accept(net::use_awaitable);
			spdlog::info("tcp_run new connection: {}:{}",
			             socket.remote_endpoint().address().to_string(),
			             socket.remote_endpoint().port());

			auto tcp_stub = std::make_shared<tcp_stub_t>(
				std::move(socket), method_group_);
			id_tcp_stub_.emplace(tcp_stub->id(), tcp_stub);
			co_await tcp_stub->run();
		}
	}

	net::awaitable<void> service::udp_run()
	{
		net::ip::udp::endpoint bind_endpoint{config_.address(), config_.port()};
		net::ip::udp::socket acceptor(co_await net::this_coro::executor);
		acceptor.open(bind_endpoint.protocol());
		acceptor.set_option(net::socket_base::reuse_address(true));
		acceptor.bind(bind_endpoint);

		std::string initial(1500, '\0');
		while (running_)
		{
			net::ip::udp::endpoint remote;
			size_t size_read = co_await acceptor.async_receive_from(
				net::buffer(initial), remote, net::use_awaitable);

			net::ip::udp::socket socket(co_await net::this_coro::executor);
			socket.open(bind_endpoint.protocol());
			socket.set_option(net::socket_base::reuse_address(true));
			socket.bind(bind_endpoint);
			socket.connect(remote);
			spdlog::info("udp_run new connection: {}:{}",
			             socket.remote_endpoint().address().to_string(),
			             socket.remote_endpoint().port());

			auto udp_stub = std::make_shared<udp_stub_t>(
				std::move(socket), method_group_);
			id_udp_stub_.emplace(udp_stub->id(), udp_stub);
			co_await udp_stub->run(net::buffer(initial, size_read));
		}
	}

	net::awaitable<Echo::Response> service::echo(const Echo::Request& request)
	{
		Echo::Response response;
		response.set_message(request.message());
		co_return response;
	}
} // namespace acc_engineer
