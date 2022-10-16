#ifndef ACC_ENGINEER_SERVER_RPC_STUB_H
#define ACC_ENGINEER_SERVER_RPC_STUB_H

// std
#include <bitset>

// boost
#include <boost/noncopyable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

// module
#include "error_code.h"
#include "methods.h"
#include "types.h"

namespace acc_engineer::rpc
{
	template <typename AsyncStream>
	class stub : public boost::noncopyable
	{
	public:
		explicit stub(AsyncStream stream, const methods& methods = methods::empty_methods());

		stub(stub&&) noexcept = delete;

		stub& operator=(stub&&) noexcept = delete;

		template <method_message Message>
		net::awaitable<result<response_t<Message>>> async_call(const request_t<Message>& request);

	private:
		enum class stub_status
		{
			idle = 1,
			running = 2,
			stopping = 3,
			stopped = 4,
		};

		using sender_channel_type = net::experimental::channel<void(sys::error_code, const std::vector<net::const_buffer>* buffer)>;

		using reply_channel_type = net::experimental::channel<void(sys::error_code, rpc::Cookie cookie, std::vector<uint8_t>)>;

		uint64_t generate_trace_id();

		net::awaitable<void> sender_loop();

		net::awaitable<void> receiver_loop();

		net::awaitable<void> dispatch_request(uint64_t cmd_id, rpc::Cookie cookie, std::vector<uint8_t> request_message_payload);

		net::awaitable<void> dispatch_response(uint64_t cmd_id, rpc::Cookie cookie, std::vector<uint8_t> response_message_payload);

		AsyncStream stream_;

		const methods& methods_;

		sender_channel_type sender_channel_;

		stub_status status_{ stub_status::idle };

		uint64_t trace_id_current_{ 0 };

		std::unordered_map<uint64_t, reply_channel_type*> calling_{};
	};

	template <typename AsyncStream>
	stub<AsyncStream>::stub(AsyncStream stream, const methods& methods) :
		stream_(std::move(stream)),
		methods_(methods),
		sender_channel_(stream_.get_executor())
	{
		status_ = stub_status::running;

		net::co_spawn(stream_.get_executor(), sender_loop(), net::detached);
		net::co_spawn(stream_.get_executor(), receiver_loop(), net::detached);
	}

	template <typename AsyncStream>
	template <method_message Message>
	net::awaitable<result<response_t<Message>>> stub<AsyncStream>::async_call(const request_t<Message>& request)
	{
		uint64_t cmd_id = Message::descriptor()->options().GetExtension(rpc::cmd_id);
		uint64_t flags = 1ULL << flag_is_request;

		rpc::Cookie request_cookie;
		request_cookie.set_trace_id(generate_trace_id());

		std::string request_cookie_payload;
		if (!request_cookie.SerializeToString(&request_cookie_payload))
		{
			co_return system_error::proto_serialize_fail;
		}

		uint64_t request_cookie_payload_size = request_cookie_payload.size();

		std::string request_message_payload;
		if (!request.SerializeToString(&request_message_payload))
		{
			co_return system_error::proto_serialize_fail;
		}

		uint64_t request_message_payload_size = request_cookie_payload.size();

		reply_channel_type reply_channel(co_await net::this_coro::executor);

		calling_[request_cookie.trace_id()] = &reply_channel;

		const std::vector<net::const_buffer> request_payloads
		{
			net::buffer(&cmd_id, sizeof(cmd_id)),
			net::buffer(&flags, sizeof(flags)),
			net::buffer(&request_cookie_payload_size, sizeof(request_cookie_payload_size)),
			net::buffer(&request_message_payload_size, sizeof(request_message_payload_size)),
			net::buffer(request_cookie_payload),
			net::buffer(request_message_payload)
		};

		co_await sender_channel_.async_send({}, &request_payloads, net::use_awaitable);

		auto [response_cookie, response_payload] = co_await reply_channel.async_receive(net::use_awaitable);

		calling_.erase(request_cookie.trace_id());

		response_t<Message> response{};
		if (!response.ParseFromArray(response_payload.data(), static_cast<int>(response_payload.size())))
		{
			co_return system_error::proto_parse_fail;
		}

		co_return std::move(response);
	}


	template <typename AsyncStream>
	uint64_t stub<AsyncStream>::generate_trace_id()
	{
		return trace_id_current_++;
	}

	template <typename AsyncStream>
	net::awaitable<void> stub<AsyncStream>::sender_loop()
	{
		while (status_ == stub_status::running)
		{
			const std::vector<net::const_buffer>* buffers_to_send = co_await sender_channel_.async_receive(net::use_awaitable);
			co_await net::async_write(stream_, *buffers_to_send, net::use_awaitable);
		}
	}

	template <typename AsyncStream>
	net::awaitable<void> stub<AsyncStream>::receiver_loop()
	{

		while (status_ == stub_status::running)
		{
			uint64_t cmd_id = 0;
			uint64_t flags = 0;
			uint64_t cookie_payload_size = 0;
			uint64_t message_payload_size = 0;

			std::array header_payload
			{
				net::buffer(&cmd_id, sizeof(cmd_id)),
				net::buffer(&flags, sizeof(flags)),
				net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
				net::buffer(&message_payload_size, sizeof(message_payload_size)),
			};

			std::vector<uint8_t> cookie_payload;
			std::vector<uint8_t> message_payload;

			co_await net::async_read(stream_, header_payload, net::use_awaitable);
			cookie_payload.resize(cookie_payload_size);
			message_payload.resize(message_payload_size);

			std::array variable_data_payload
			{
				net::buffer(cookie_payload),
				net::buffer(message_payload)
			};

			co_await net::async_read(stream_, variable_data_payload, net::use_awaitable);

			rpc::Cookie cookie;
			if (!cookie.ParseFromArray(cookie_payload.data(), static_cast<int>(cookie_payload.size())))
			{
				// TODO should directly close stream_
			}

			if (flags & (1ULL << flag_is_request))
			{
				co_await dispatch_request(cmd_id, std::move(cookie), message_payload);
			}
			else
			{
				co_await dispatch_response(cmd_id, std::move(cookie), message_payload);
			}

			cookie_payload.clear();
			message_payload.clear();
		}
	}

	template <typename AsyncStream>
	net::awaitable<void> stub<AsyncStream>::dispatch_request(uint64_t cmd_id, rpc::Cookie cookie, std::vector<uint8_t> request_message_payload)
	{
		auto run_method = [this, cookie = std::move(cookie), request_payload = std::move(request_message_payload)]() mutable -> net::awaitable<void>
		{
			std::string response_message_payload = co_await std::invoke(methods_, );
		};

		net::co_spawn(co_await net::this_coro::executor, run_method, net::detached);
	}

	template <typename AsyncStream>
	net::awaitable<void> stub<AsyncStream>::dispatch_response(uint64_t, rpc::Cookie cookie, std::vector<uint8_t> response_message_payload)
	{
		const auto calling = calling_.find(cookie.trace_id());
		if (calling == calling_.end())
		{
			co_return;
		}
		
		co_await calling->second->async_send({}, std::move(cookie), std::move(response_message_payload), net::use_awaitable);
	}
}

#endif //ACC_ENGINEER_SERVER_RPC_STUB_H
