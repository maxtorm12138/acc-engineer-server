#ifndef ACC_ENGINEER_SERVER_RPC_H
#define ACC_ENGINEER_SERVER_RPC_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/redirect_error.hpp>

#include <boost/core/noncopyable.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/scope_exit.hpp>
#include "service.pb.h"

namespace acc_engineer
{
	namespace net = boost::asio;
	namespace sys = boost::system;

	class rpc_caller : public boost::noncopyable
	{
	public:
		net::awaitable<void> run();

		template<typename Method>
		net::awaitable<sys::error_code> async_call_rpc(Method &method)
		{
			auto executor = co_await net::this_coro::executor;

			auto uuid = uuid_generator_();
			std::string request_id(uuid.begin(), uuid.end());
			method.mutable_common()->set_request_id(request_id);

			message_channel_t channel(executor, 1);
			calling_.emplace(request_id, &channel);

			sys::error_code ec;

			ec = co_await channel.async_send({}, method.SerializeAsString(), net::use_awaitable);
			if (ec)
			{
				calling_.erase(request_id);
				co_return ec;
			}

			auto response = co_await channel.async_receive(net::redirect_error(net::use_awaitable, ec));
			if (ec)
			{
				calling_.erase(request_id);
				co_return ec;
			}

			calling_.erase(request_id);
		}

	private:
		using message_channel_t = net::experimental::channel<void(sys::error_code, std::string)>;
		boost::uuids::random_generator uuid_generator_;
		std::unordered_map<std::string, message_channel_t *> calling_;
	};

}

#endif // !ACC_ENGINEER_SERVER_RPC_H
