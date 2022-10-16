#ifndef ACC_ENGINEER_SERVER_RPC_METHODS_H
#define ACC_ENGINEER_SERVER_RPC_METHODS_H

#include <unordered_map>

#include <boost/asio/awaitable.hpp>

#include "detail/method_type_erasure.h"
#include "types.h"

namespace acc_engineer::rpc
{
	namespace detail
	{
		template <method_message Message, typename AwaitableMethodImplement>
			requires std::invocable<AwaitableMethodImplement, const rpc::Cookie&, const request_t<Message>&>&&
		std::same_as<
			std::invoke_result_t<AwaitableMethodImplement, const rpc::Cookie&, const request_t<Message>&>,
			net::awaitable<result<response_t<Message>>>>
			class method final : public detail::method_type_erasure
		{
		public:
			explicit method(AwaitableMethodImplement&& implement) : implement_(
				std::forward<AwaitableMethodImplement>(implement))
			{
			}

			net::awaitable<result<std::vector<uint8_t>>> operator()(
				uint64_t cmd_id,
				rpc::Cookie& cookie,
				const std::vector<uint8_t>& request_message_payload) override
			{
				request_t<Message> request{};
				if (!request.ParseFromArray(request_message_payload.data(), request_message_payload.size()))
				{
					co_return system_error::proto_parse_fail;
				}

				result<response_t<Message>> result = co_await std::invoke(implement_, std::cref(cookie), std::cref(request));
				if (result.error())
				{
					co_return result.error();
				}

				std::vector<uint8_t> response_message_payload;
				if (!result.value().SerializeToArray(response_message_payload.data(), response_message_payload.size()))
				{
					co_return system_error::proto_serialize_fail;
				}

				co_return std::move(response_message_payload);
			}

		private:
			AwaitableMethodImplement implement_;
		};
	}

	class methods : public boost::noncopyable
	{
	public:
		static methods& empty_methods()
		{
			static methods empty_methods;
			return empty_methods;
		}

		template <method_message Message, typename AwaitableMethodImplement>
			requires std::invocable<AwaitableMethodImplement, const rpc::Cookie&, const request_t<Message>&>&&
		std::same_as<
			std::invoke_result_t<AwaitableMethodImplement, const rpc::Cookie&, const request_t<Message>&>,
			net::awaitable<result<response_t<Message>>>>
			methods & implement(uint64_t cmd_id, AwaitableMethodImplement&& implement)
		{
			auto [iterator, exists] = implements_.emplace(
				cmd_id, std::make_unique<detail::method<Message, AwaitableMethodImplement>>(std::forward<AwaitableMethodImplement>(implement)));
			if (exists)
			{
				throw std::runtime_error("cmd_id already registered");
			}

			return *this;
		}

		net::awaitable<result<std::vector<uint8_t>>> operator()(uint64_t cmd_id, rpc::Cookie& cookie, const std::vector<uint8_t>& request_message_payload) const
		{
			const auto implement = implements_.find(cmd_id);
			if (implement == implements_.end())
			{
				co_return system_error::method_not_implement;
			}

			co_return co_await std::invoke(*implement->second, cmd_id, cookie, request_message_payload);
		}

	private:

		std::unordered_map<uint64_t, std::unique_ptr<detail::method_type_erasure>> implements_;
	};
}

#endif
