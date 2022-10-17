#include "service.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include "service.pb.h"

namespace net = boost::asio;

namespace acc_engineer
{

    service::service(config cfg) :
            config_(std::move(cfg))
    {
    }

    net::awaitable<void> service::run()
    {
        using namespace std::placeholders;
        running_ = true;
        method_group_.implement<Echo>(0, std::bind(&service::echo, this, _1));

        net::co_spawn(co_await net::this_coro::executor, unix_domain_run(), net::detached);
        net::co_spawn(co_await net::this_coro::executor, udp_read_run(), net::detached);
        net::co_spawn(co_await net::this_coro::executor, tcp_run(), net::detached);
    }

    net::awaitable<void> service::new_tcp_connection(net::ip::tcp::socket socket)
    {
        auto tcp_stub = std::make_shared<tcp_stub_t>(std::move(socket), method_group_);
        id_tcp_stub_.emplace(tcp_stub->id(), tcp_stub);
        co_await tcp_stub->run();
    }

    net::awaitable<void> service::tcp_run()
    {
        auto executor = co_await net::this_coro::executor;
        net::ip::tcp::acceptor acceptor(executor, net::ip::tcp::endpoint{config_.address(), config_.port()});

        while (running_)
        {
            auto socket = co_await acceptor.async_accept(net::use_awaitable);
            if (socket.is_open())
            {
                net::co_spawn(executor, new_tcp_connection(std::move(socket)), net::detached);
            }
        }
    }

    net::awaitable<void> service::udp_read_run()
    {
        net::ip::udp::socket acceptor(co_await net::this_coro::executor, net::ip::udp::endpoint{config_.address(), config_.port()});
        acceptor.set_option(boost::asio::socket_base::reuse_address(true));
        std::vector<uint8_t> buffer(8192);
        while (running_)
        {
            net::ip::udp::endpoint remote;
            size_t size_read = co_await acceptor.async_receive_from(net::buffer(buffer), remote, net::use_awaitable);

            if (!ep_unix_socket_.contains(remote))
            {
                net::local::stream_protocol::endpoint endpoint("acc-engineer-server.socket");
                auto unix_socket = std::make_shared<unix_socket_t>(co_await net::this_coro::executor);
                unix_socket->connect(endpoint);

                ep_unix_socket_.emplace(remote, unix_socket);
                net::co_spawn(co_await net::this_coro::executor, udp_write_run(acceptor, remote, unix_socket), net::detached);
            }

            co_await net::async_write(*ep_unix_socket_[remote], net::buffer(buffer, size_read), net::use_awaitable);
        }
    }

    net::awaitable<void> service::udp_write_run(
            net::ip::udp::socket &acceptor,
            net::ip::udp::endpoint remote, std::shared_ptr<unix_socket_t> unix_socket)
    {
        while (running_)
        {
            uint64_t command_id = 0;
            uint64_t flags = 0;
            uint64_t cookie_payload_size = 0;
            uint64_t message_payload_size = 0;

            std::array header_payload
                    {
                            net::buffer(&command_id, sizeof(command_id)),
                            net::buffer(&flags, sizeof(flags)),
                            net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
                            net::buffer(&message_payload_size, sizeof(message_payload_size)),
                    };


            co_await net::async_read(*unix_socket, header_payload, net::use_awaitable);
            rpc::payload_t cookie_payload(cookie_payload_size, '\0');
            rpc::payload_t message_payload(message_payload_size, '\0');

            std::array variable_data_payload
                    {
                            net::buffer(cookie_payload),
                            net::buffer(message_payload)
                    };
            co_await net::async_read(*unix_socket, variable_data_payload, net::use_awaitable);

            const std::array payloads
                    {
                            net::buffer(&command_id, sizeof(command_id)),
                            net::buffer(&flags, sizeof(flags)),
                            net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
                            net::buffer(&message_payload_size, sizeof(message_payload_size)),
                            net::buffer(cookie_payload),
                            net::buffer(message_payload)
                    };

            co_await acceptor.async_send_to(payloads, remote, net::use_awaitable);
        }
    };

    net::awaitable<void> service::unix_domain_run()
    {
        net::local::stream_protocol::endpoint endpoint("acc-engineer-server.socket");
        net::local::stream_protocol::acceptor acceptor(co_await net::this_coro::executor, endpoint);

        while (running_)
        {
            unix_socket_t unix_socket(co_await net::this_coro::executor);
            co_await acceptor.async_accept(unix_socket, net::use_awaitable);
            net::co_spawn(co_await net::this_coro::executor, new_unix_domain_connection(std::move(unix_socket)), net::detached);
        }
    }

    net::awaitable<void> service::new_unix_domain_connection(unix_socket_t socket)
    {
        auto unix_domain_stub = std::make_shared<unix_domain_stub_t>(std::move(socket), method_group_);
        id_unix_domain_stub_.emplace(unix_domain_stub->id(), unix_domain_stub);
        co_await unix_domain_stub->run();
    }

    net::awaitable<Echo::Response> service::echo(const Echo::Request &request)
    {
        Echo::Response response;
        response.set_message(request.message());
        co_return response;
    }
}