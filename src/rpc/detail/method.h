#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H

// boost
#include <boost/asio/awaitable.hpp>

// spdlog
#include <spdlog/spdlog.h>

#include "error_code.h"
#include "method_type_erasure.h"
#include "type_requirements.h"
#include "types.h"


namespace acc_engineer::rpc::detail
{
	namespace net = boost::asio;

	template <method_message Message, typename Implement>
		requires method_implement<Message, Implement>
	class method final : public method_type_erasure
	{
	public:
		explicit method(Implement&& implement);

		net::awaitable<std::string> operator(
		)(std::string request_message_payload) override;

	private:
		Implement implement_;
	};

	template <method_message Message, typename Implement>
		requires method_implement<Message, Implement>
	method<Message, Implement>::method(Implement&& implement)
		: implement_(std::forward<Implement>(implement))
	{
	}

	template <method_message Message, typename Implement>
		requires method_implement<Message, Implement>
	net::awaitable<std::string> method<Message, Implement>::operator()(
		std::string request_message_payload)
	{
		request_t<Message> request{};
		if (!request.ParseFromString(request_message_payload.data()))
		{
			spdlog::error("method invoke error, parse request fail");
			throw sys::system_error(system_error::proto_parse_fail);
		}

		response_t<Message> response = co_await std::invoke(
			implement_, std::cref(request));
		spdlog::debug(
			R"(method invoke method: {} request: [{}] response: [{}])",
			Message::descriptor()->full_name(),
			request.ShortDebugString(),
			response.ShortDebugString());

		std::string response_message_payload;
		if (!response.SerializeToString(&response_message_payload))
		{
			spdlog::error("method invoke error, serialize response fail");
			throw sys::system_error(system_error::proto_serialize_fail);
		}

		co_return std::move(response_message_payload);
	}
}

#endif //ACC_ENGINEER_SERVER_RPC_DETAIL_METHOD_H
