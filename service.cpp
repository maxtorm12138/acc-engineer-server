#include "service.h"

#include <random>

#include <utility>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "service.pb.h"

namespace net = boost::asio;

namespace acc_engineer
{
    namespace detail
    {
        template<typename Message>
        net::awaitable<boost::system::error_code> fetch(detail::tcp_socket &socket)
        {

        };
    }


    service::service(config cfg) :
        config_(std::move(cfg))
    {
    }

    net::awaitable<void> service::run()
    {
        auto executor = co_await net::this_coro::executor;
        acceptor_.emplace(executor, net::ip::tcp::endpoint{config_.address(), config_.port()});

        running_ = true;
        while(running_)
        {
            auto [ec, socket] = co_await acceptor_->async_accept();
            if (socket.is_open())
            {
                net::co_spawn(executor, handshake(std::move(socket)), net::detached);
            }
        }
    }

    net::awaitable<void> service::stop()
    {
        running_ = false;
        co_return;
    }

    net::awaitable<void> service::handshake(detail::tcp_socket socket)
    {
        uint16_t length = 0;
        auto [ec, size] = co_await net::async_read(socket, net::buffer(&length, sizeof(length)));
        if (ec || size != sizeof(length))
        {
            co_return ;
        }

        co_return ;
    }
}