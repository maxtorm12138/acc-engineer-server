#ifndef ACC_ENGINEER_SERVER_RPC_H
#define ACC_ENGINEER_SERVER_RPC_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "service.pb.h"

namespace acc_engineer
{
	namespace net = boost::asio;
	namespace sys = boost::system;

	template<typename Executor>
	class rpc : public boost::noncopyable
	{
	public:
		net::awaitable<void> run();

	private:
		std::optional<net::experimental::channel<void(sys::error_code, std::unique_ptr<google::protobuf::Message>)>> send_channel_;
	};

}

#endif // !ACC_ENGINEER_SERVER_RPC_H
