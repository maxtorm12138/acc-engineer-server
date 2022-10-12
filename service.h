#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

#include <unordered_map>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>

#include "config.h"

#include "service.pb.h"

namespace acc_engineer
{
    struct session
    {
        std::string ticket;
        std::unique_ptr<boost::asio::ip::tcp::socket> tcp_socket_;
    };

    class service
    {
    public:
        service(boost::asio::io_context &io_context, const config &cfg);

        void run();

    private:
        boost::asio::awaitable<void> listen();
        boost::asio::awaitable<void> handshake(boost::asio::ip::tcp::socket socket);

    private:
        boost::asio::awaitable<void> issue_ticket(const IssueTicketRequest &req, boost::asio::ip::tcp::socket socket);
        boost::asio::awaitable<void> client_connection(const ClientConnectionRequest &req, boost::asio::ip::tcp::socket socket);

    private:
        boost::asio::io_context &io_context_;
        boost::asio::ip::tcp::endpoint tcp_endpoint_;
        boost::asio::ip::udp::endpoint udp_endpoint_;
        std::unique_ptr<boost::asio::ip::tcp::acceptor> tcp_acceptor_;
        std::unique_ptr<boost::asio::ip::udp::socket> udp_socket_;

        std::unordered_map<std::string, session> sessions_;
    };
}


#endif //ACC_ENGINEER_SERVER_SERVICE_H
