#ifndef ACC_ENGINEER_SERVER_DETAIL_RPC_TYPES_H
#define ACC_ENGINEER_SERVER_DETAIL_RPC_TYPES_H

#include <array>
#include <concepts>
#include <google/protobuf/message.h>

namespace acc_engineer::rpc
{
	namespace net = boost::asio;
	namespace sys = boost::system;

	template<typename Message>
	concept method_message =
		requires
	{
		std::derived_from<Message, google::protobuf::Message>;
		std::derived_from<typename Message::Response, google::protobuf::Message>;
		std::derived_from<typename Message::Request, google::protobuf::Message>;
	};

	template<method_message M>
	struct request
	{
		using type = typename M::Request;
	};

	template<method_message M>
	using request_t = typename request<M>::type;

	using request_id_t = std::array<uint8_t, 16>;

	template<method_message M>
	struct response
	{
		using type = typename M::Response;
	};

	template<method_message M>
	using response_t = typename response<M>::type;

	enum flags
	{
		flag_is_request = 0,
	};

}

#endif //ACC_ENGINEER_SERVER_DETAIL_RPC_TYPES_H
