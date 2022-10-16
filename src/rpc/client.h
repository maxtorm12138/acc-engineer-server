#ifndef ACC_ENGINEER_SERVER_RPC_CLIENT_H
#define ACC_ENGINEER_SERVER_RPC_CLIENT_H

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>

#include <boost/noncopyable.hpp>

#include <boost/uuid/uuid_generators.hpp>

#include "types.h"
#include "result.h"
#include "proto/rpc.pb.h"

namespace acc_engineer::rpc
{
    template<typename AsyncStream>
    class client : public boost::noncopyable
    {
    public:

        explicit client(AsyncStream &stream) :
			stream_(stream)
        {}

        client(client &&other) noexcept :
            stream_(other.stream_)
        {}

    public:
        net::awaitable<void> setup()
        {
			auto executor = co_await net::this_coro::executor;
			send_channel_.emplace(executor, 50);
			
			net::co_spawn(executor, receiver(), net::detached);
			net::co_spawn(executor, sender(), net::detached);
        }

        template<method_message Message>
        net::awaitable<result<response_t<Message>>> async_call(const request_t<Message> &request)
        {
            using result_type = result<response_t<Message>>;

            auto executor = co_await net::this_coro::executor;

            auto request_id = generate_request_id();
            auto cmd_id = Message::descriptor()->options().GetExtension(rpc::cmd_id);

            auto receive_channel = std::make_shared<message_channel_t>(executor, 1);

            calling_.emplace(request_id, receive_channel);

            sys::error_code ec;
            co_await send_channel_->async_send({}, cmd_id, request_id, request.SerializeAsString(), net::redirect_error(net::use_awaitable, ec));
            if (ec)
            {
                calling_.erase(request_id);
                co_return result_type(ec);
            }

            auto[_, _1, response_payloads] = co_await receive_channel->async_receive(net::redirect_error(net::use_awaitable, ec));
            if (ec)
            {
                calling_.erase(request_id);
                co_return result_type(ec);
            }

            calling_.erase(request_id);

            response_t<Message> response{};
            response.ParseFromString(response_payloads);


            co_return result_type(std::move(response));
        }

    private:

        request_id_t generate_request_id()
        {
			std::array<uint8_t, 16> request_id{};
        	auto uuid = uuid_generator_();
        	std::copy(uuid.begin(), uuid.end(), request_id.begin());
        	return request_id;
        }

        net::awaitable<void> receiver()
        {
			for (;;)
        	{
        	    uint64_t cmd_id{0};
        	    std::array<uint8_t, 16> request_id{};
        	    uint64_t response_size{0};

        	    std::array<net::mutable_buffer, 3> header_payloads{};
        	    header_payloads[0] = net::buffer(&cmd_id, sizeof(cmd_id));
        	    header_payloads[1] = net::buffer(request_id);
        	    header_payloads[2] = net::buffer(&response_size, sizeof(response_size));

        	    co_await net::async_read(*stream_, header_payloads, net::use_awaitable);

        	    std::string response_payloads(response_size, '\0');
        	    co_await net::async_read(*stream_, net::buffer(response_payloads), net::use_awaitable);

        	    auto receive_channel = calling_[request_id];
        	    co_await receive_channel->async_send({}, cmd_id, request_id, response_payloads, net::use_awaitable);
        	}
        }

        net::awaitable<void> sender()
        {
			for (;;)
        	{
        	    auto[cmd_id, request_id, request_payload] = co_await send_channel_->async_receive(net::use_awaitable);
        	    uint64_t request_size = request_payload.size();

        	    std::array<net::const_buffer, 4> payloads{};
        	    payloads[0] = net::buffer(&cmd_id, sizeof(cmd_id));
        	    payloads[1] = net::buffer(request_id);
        	    payloads[2] = net::buffer(&request_size, sizeof(request_size));
        	    payloads[3] = net::buffer(request_payload);

        	    co_await net::async_write(*stream_, payloads, net::use_awaitable);
        	}
        }

        std::add_const_t<std::add_lvalue_reference_t<AsyncStream>> stream_;

        boost::uuids::random_generator uuid_generator_;
        std::optional<message_channel_t> send_channel_;
        std::map<request_id_t, std::shared_ptr<message_channel_t>> calling_;
    };
}
#endif //ACC_ENGINEER_SERVER_RPC_CLIENT_H
