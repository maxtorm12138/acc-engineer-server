#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

#include <unordered_map>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>

#include "config.h"

#include "service.pb.h"

namespace acc_engineer
{
    namespace net = boost::asio;

    namespace detail
    {
        using awaitable_tuple_t = net::as_tuple_t<net::use_awaitable_t<>>;
        using tcp_acceptor = awaitable_tuple_t::as_default_on_t<net::ip::tcp::acceptor>;
        using tcp_socket = awaitable_tuple_t::as_default_on_t<net::ip::tcp::socket>;
        using udp_socket = awaitable_tuple_t::as_default_on_t<net::ip::udp::socket>;

    }

    struct session
    {
        std::string ticket;
        std::string driver_name;
        uint64_t driver_id;
        net::ip::tcp::socket tcp_socket_;
        net::ip::udp::endpoint udp_endpoint_;
    };

    class service
    {
    public:
        service(config cfg);

        net::awaitable<void> run();
        net::awaitable<void> stop();

    private:
        net::awaitable<void> handshake(detail::tcp_socket socket);

    private:
        config config_;
        bool running_{false};
        std::optional<detail::tcp_acceptor> acceptor_;
        std::unordered_map<std::string, session> sessions_;
    };
}


#endif //ACC_ENGINEER_SERVER_SERVICE_H
