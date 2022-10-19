#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

#include <unordered_map>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#include "config.h"
#include "rpc/stub.h"

#include "proto/service.pb.h"

namespace acc_engineer
{
	namespace net = boost::asio;

	using tcp_stub_t = rpc::stream_stub<net::ip::tcp::socket>;
	using udp_stub_t = rpc::datagram_stub<net::ip::udp::socket>;

	class service
	{
	public:
		service(config cfg);

		net::awaitable<void> run();

	private:
		net::awaitable<void> tcp_run();

		net::awaitable<void> udp_run();

		net::awaitable<Echo::Response> echo(const Echo::Request& request);
		//net::awaitable<rpc::result<Authentication::Response>> authentication(const Authentication::Request &request);


		config config_;
		bool running_{false};
		rpc::method_group method_group_;

		std::unordered_map<uint64_t, std::shared_ptr<tcp_stub_t>> id_tcp_stub_;
		std::unordered_map<uint64_t, std::shared_ptr<udp_stub_t>> id_udp_stub_;
	};
} // namespace acc_engineer

template <>
inline size_t
std::hash<boost::asio::ip::udp::endpoint>::operator()(
	const boost::asio::ip::udp::endpoint& ep) const noexcept { return 1; }

#endif//ACC_ENGINEER_SERVER_SERVICE_H
