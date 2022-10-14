#include "rpc.h"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

namespace acc_engineer::rpc
{
    client_service::client_service(net::ip::tcp::socket &socket) : socket_(socket)
    {}

    net::awaitable<void> client_service::run()
    {
        auto executor = co_await net::this_coro::executor;
        send_channel_.emplace(executor, 50);

        net::co_spawn(executor, receiver(), net::detached);
        net::co_spawn(executor, sender(), net::detached);
    }

    net::awaitable<void> client_service::sender()
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

            co_await net::async_write(socket_, payloads, net::use_awaitable);
        }
    }

    net::awaitable<void> client_service::receiver()
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

            co_await net::async_read(socket_, header_payloads, net::use_awaitable);

            std::string response_payloads(response_size, '\0');
            co_await net::async_read(socket_, net::buffer(response_payloads), net::use_awaitable);

            auto receive_channel = calling_[request_id];
            co_await receive_channel->async_send({}, cmd_id, request_id, response_payloads, net::use_awaitable);
        }
    }

    std::array<uint8_t, 16> client_service::generate_request_id()
    {
        std::array<uint8_t, 16> request_id{};
        auto uuid = uuid_generator_();
        std::copy(uuid.begin(), uuid.end(), request_id.begin());
        return request_id;
    }

    void server_service::register_method(uint64_t cmd_id, awaitable_method_t &&method)
    {
        methods_.emplace(cmd_id, std::forward<awaitable_method_t>(method));
    }

    server_service::server_service(boost::asio::ip::tcp::socket &socket) : socket_(socket)
    {
    }

    net::awaitable<void> server_service::run()
    {
        auto executor = co_await net::this_coro::executor;
        for (;;)
        {
            uint64_t cmd_id{0};
            std::array<uint8_t, 16> request_id{};
            uint64_t request_size{0};

            std::array<net::mutable_buffer, 3> header_payloads{};
            header_payloads[0] = net::buffer(&cmd_id, sizeof(cmd_id));
            header_payloads[1] = net::buffer(request_id);
            header_payloads[2] = net::buffer(&request_size, sizeof(request_size));

            co_await net::async_read(socket_, header_payloads, net::use_awaitable);

            std::string request_payloads(request_size, '\0');
            co_await net::async_read(socket_, net::buffer(request_payloads), net::use_awaitable);

            net::co_spawn(executor, invoke(cmd_id, request_id, std::move(request_payloads)), net::detached);
        }
    }

    net::awaitable<void> server_service::invoke(uint64_t cmd_id, request_id_t request_id, std::string request_payloads)
    {
        auto response_payloads = co_await methods_[cmd_id](std::move(request_payloads));
        uint64_t response_size = response_payloads.size();

        std::array<net::const_buffer, 4> payloads{};
        payloads[0] = net::buffer(&cmd_id, sizeof(cmd_id));
        payloads[1] = net::buffer(request_id);
        payloads[2] = net::buffer(&response_size, sizeof(response_size));
        payloads[3] = net::buffer(response_payloads);

        co_await net::async_write(socket_, payloads, net::use_awaitable);
    }

    std::unordered_map<uint64_t, awaitable_method_t> server_service::methods_;

}