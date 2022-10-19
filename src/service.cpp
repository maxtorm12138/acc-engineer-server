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
        method_group_.implement<Echo>(std::bind(&service::echo, this, _1));

        net::co_spawn(co_await net::this_coro::executor, udp_run(), net::detached);
        co_await net::co_spawn(co_await net::this_coro::executor, tcp_run(), net::use_awaitable);
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

    net::awaitable<void> service::udp_run()
    {
        net::ip::udp::socket acceptor(co_await net::this_coro::executor, net::ip::udp::endpoint{config_.address(), config_.port()});
        acceptor.set_option(boost::asio::socket_base::reuse_address(true));

        std::string initial(1500, '\0');
        while (running_)
        {
            net::ip::udp::endpoint remote;
            size_t size_read = co_await acceptor.async_receive_from(net::buffer(initial), remote, net::use_awaitable);

            net::ip::udp::socket socket(co_await net::this_coro::executor);
            if (remote.address().is_v4())
            {
                socket.open(net::ip::udp::v4());
            }
            else if (remote.address().is_v6())
            {
                socket.open(net::ip::udp::v6());
            }
            socket.set_option(net::socket_base::reuse_address(true));
            socket.bind({config_.address(), config_.port()});
            socket.connect(remote);

            auto udp_stub = std::make_shared<udp_stub_t>(std::move(socket), method_group_);
            id_udp_stub_.emplace(udp_stub->id(), udp_stub);
            co_await udp_stub->run(net::buffer(initial));
        }
    }

    net::awaitable<Echo::Response> service::echo(const Echo::Request &request)
    {
        Echo::Response response;
        response.set_message(request.message());
        co_return response;
    }
}