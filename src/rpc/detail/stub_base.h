#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_BASE_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_BASE_H

// std
#include <bitset>

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/scope_exit.hpp>

// module
#include "../method_group.h"
#include "timed_operation.h"
#include "type_requirements.h"
#include "types.h"

// protocol
#include "proto/rpc.pb.h"


namespace acc_engineer::rpc::detail
{
	enum class stub_status
	{
		idle = 1,
		running = 2,
		stopping = 3,
		stopped = 4,
	};

	class stub_base
	{
	public:
		explicit stub_base(const method_group& method_group);

		[[nodiscard]] uint64_t id() const;

		[[nodiscard]] stub_status status() const;

	protected:
		[[nodiscard]] std::string pack(uint64_t command_id, std::bitset<64> bit_flags,
		                               const Cookie& cookie,
		                               const std::string& message_payload) const;

		[[nodiscard]] std::tuple<uint64_t, std::bitset<64>, Cookie, std::string>
		unpack(net::const_buffer payload) const;

		static uint64_t generate_trace_id();

		net::awaitable<void> dispatch(sender_channel_t& sender_channel,
		                              net::const_buffer payload);

		net::awaitable<void>
		invoke_method(sender_channel_t& sender_channel, uint64_t command_id,
		              std::bitset<64> bit_flags, Cookie cookie,
		              std::string message_payload);

		template <method_message Message>
		net::awaitable<response_t<Message>> do_async_call(
			sender_channel_t& sender_channel, const request_t<Message>& request);

		const method_group& method_group_;
		const uint64_t stub_id_{stub_id_max_++};
		stub_status status_{stub_status::idle};
		std::unordered_map<uint64_t, reply_channel_t*> calling_{};

	private:
		static std::atomic<uint64_t> stub_id_max_;
		static std::atomic<uint64_t> trace_id_max_;
	};

	template <method_message Message>
	net::awaitable<response_t<Message>> stub_base::do_async_call(
		sender_channel_t& sender_channel, const request_t<Message>& request)
	{
		const uint64_t command_id = Message::descriptor()->options().
		                                                   GetExtension(cmd_id);
		const bool is_no_reply = Message::descriptor()->options().
		                                                GetExtension(no_reply);
		const auto flags = std::bitset<64>{}.set(flag_is_request, true).set(
			flag_no_reply, is_no_reply);

		Cookie request_cookie;
		request_cookie.set_trace_id(generate_trace_id());
		request_cookie.set_error_code(0);

		std::string request_message_payload;
		if (!request.SerializeToString(&request_message_payload))
		{
			throw sys::system_error(system_error::proto_serialize_fail);
		}

		auto request_payload = pack(command_id, flags, request_cookie,
		                            request_message_payload);

		if (is_no_reply)
		{
			co_await sender_channel.async_send({}, std::move(request_payload),
			                                   net::use_awaitable);
			co_return response_t<Message>{};
		}

		reply_channel_t reply_channel(co_await net::this_coro::executor, 1);
		this->calling_[request_cookie.trace_id()] = &reply_channel;
		BOOST_SCOPE_EXIT_ALL(&)
		{
			spdlog::debug("{} do_async_call {} erased", id(),
			              request_cookie.trace_id());
			this->calling_.erase(request_cookie.trace_id());
		};

		co_await sender_channel.async_send({}, std::move(request_payload),
		                                   net::use_awaitable);

		auto [response_cookie, response_message_payload] = co_await reply_channel.
			async_receive(net::use_awaitable);

		if (response_cookie.error_code() != 0)
		{
			throw sys::system_error(
				static_cast<system_error>(response_cookie.error_code()));
		}

		response_t<Message> response{};
		if (!response.ParseFromString(response_message_payload))
		{
			throw sys::system_error(system_error::proto_parse_fail);
		}

		co_return std::move(response);
	}
} // namespace acc_engineer::rpc::detail

#endif//ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_BASE_H
