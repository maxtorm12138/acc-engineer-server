#ifndef ACC_ENGINEER_SERVER_RPC_SERVER_H
#define ACC_ENGINEER_SERVER_RPC_SERVER_H

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

#include "types.h"
#include "detail/method.h"

namespace acc_engineer::rpc
{
	template<typename AsyncStream>
    class server : public boost::noncopyable
    {
    public:
        template<method_message Message, typename AwaitableMethod>
        static void register_method(uint64_t cmd_id, AwaitableMethod &&m)
        {
            std::unique_ptr<detail::method_base> real_method(new detail::method<Message, AwaitableMethod>(std::forward<AwaitableMethod>(m)));
            methods_.emplace(cmd_id, std::move(real_method));
        }

    public:
        explicit server(std::shared_ptr<AsyncStream> stream) :
			stream_(stream)
        {
        }


    public:
        net::awaitable<void> run()
        {
			auto executor = co_await net::this_coro::executor;
            for (;;)
            {
                uint64_t cmd_id{ 0 };
                std::array<uint8_t, 16> request_id{};
                uint64_t request_size{ 0 };

                std::array<net::mutable_buffer, 3> header_payloads{};
                header_payloads[0] = net::buffer(&cmd_id, sizeof(cmd_id));
                header_payloads[1] = net::buffer(request_id);
                header_payloads[2] = net::buffer(&request_size, sizeof(request_size));

                co_await net::async_read(*stream_, header_payloads, net::use_awaitable);

                std::string request_payloads(request_size, '\0');
                co_await net::async_read(*stream_, net::buffer(request_payloads), net::use_awaitable);

                net::co_spawn(executor, invoke(cmd_id, request_id, std::move(request_payloads)), net::detached);
            }
        }

        net::awaitable<void> invoke(uint64_t cmd_id, request_id_t request_id, std::string request_payload)
        {
			auto response_payload = co_await (*methods_[cmd_id])(cmd_id, request_id, std::move(request_payload));
        	uint64_t response_size = response_payload.size();

        	std::array<net::const_buffer, 4> payloads{};
        	payloads[0] = net::buffer(&cmd_id, sizeof(cmd_id));
        	payloads[1] = net::buffer(request_id);
        	payloads[2] = net::buffer(&response_size, sizeof(response_size));
        	payloads[3] = net::buffer(response_payload);

        	co_await net::async_write(*stream_, payloads, net::use_awaitable);
        }

    private:
        std::shared_ptr<AsyncStream> stream_;
        static std::unordered_map<uint64_t, std::unique_ptr<detail::method_base>> methods_;
    };
}

#endif //ACC_ENGINEER_SERVER_RPC_SERVER_H
