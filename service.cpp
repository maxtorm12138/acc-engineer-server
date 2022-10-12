#include "service.h"

#include <random>

#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "service.pb.h"

namespace net = boost::asio;

namespace acc_engineer::detail
{
    template<typename Message>
    net::awaitable<Message> fetch_message(net::ip::tcp::socket &socket, size_t size)
    {
        std::vector<uint8_t> buffer(size);
        co_await net::async_read(socket, net::buffer(buffer), net::use_awaitable);

        Message message;
        message.ParseFromArray(buffer.data(), buffer.size());

        co_return message;
    }

    template<typename Message>
    net::awaitable<void> reply_message(net::ip::tcp::socket &socket, Method method,const Message &msg)
    {
        uint16_t header[2];
        header[0] = method;
        header[1] = msg.ByteSizeLong();
        co_await net::async_write(socket, net::buffer(header), net::use_awaitable);

        auto data = msg.SerializeAsString();
        co_await net::async_write(socket, net::buffer(data), net::use_awaitable);
    }

    uint64_t generate_id()
    {
        static uint64_t id = 1;
        return id++;
    }
}

namespace acc_engineer
{
    service::service(boost::asio::io_context &io_context, const config &cfg) :
            io_context_(io_context),
            tcp_endpoint_(cfg.address(), cfg.port()),
            udp_endpoint_(cfg.address(), cfg.port()),
            tcp_acceptor_(nullptr)
    {
    }

    void service::run()
    {
        net::co_spawn(io_context_, [this] () { return listen(); }, net::detached);
    }

    net::awaitable<void> service::listen()
    {
        auto executor = co_await net::this_coro::executor;
        tcp_acceptor_ = std::make_unique<net::ip::tcp::acceptor>(executor, tcp_endpoint_);
        udp_socket_ = std::make_unique<net::ip::udp::socket>(executor, udp_endpoint_);

        for (;;)
        {
            auto socket = co_await tcp_acceptor_->async_accept(net::use_awaitable);
            net::co_spawn(executor, [this, s = std::move(socket)] () mutable { return handshake(std::move(s)); }, net::detached);
        }
    }

    net::awaitable<void> service::handshake(boost::asio::ip::tcp::socket socket)
    {
        uint16_t header[2];
        co_await net::async_read(socket, net::buffer(header), net::use_awaitable);
        switch (header[0])
        {
            case Method::IssueTicket:
            {
                auto request = co_await detail::fetch_message<IssueTicketRequest>(socket, header[1]);
                co_await issue_ticket(request, std::move(socket));
            }
                break;
            case Method::ClientConnection:
            {
                auto request = co_await detail::fetch_message<ClientConnectionRequest>(socket, header[1]);
                co_await client_connection(request, std::move(socket));
            }
                break;
        }
    }

    net::awaitable<void> service::issue_ticket(const IssueTicketRequest &req, boost::asio::ip::tcp::socket socket)
    {
        auto ticket = boost::uuids::to_string(boost::uuids::random_generator{}());
        auto id = detail::generate_id();

        IssueTicketResponse response;
        response.set_ticket(ticket);
        response.set_error_code(0);
        response.set_id(id);
        response.set_error_message("success");

        co_await detail::reply_message(socket, Method::IssueTicket, response);

        co_await online_notify(ticket, req.username(), id);

        session s;
        s.ticket = ticket;
        s.username = req.username();
        s.id = id;
        s.tcp_socket_ = std::make_unique<net::ip::tcp::socket>(std::move(socket));

        sessions_.emplace(ticket, std::move(s));
    }

    net::awaitable<void> service::client_connection(const ClientConnectionRequest &req, boost::asio::ip::tcp::socket socket)
    {
        ClientConnectionResponse response;

        if (sessions_.count(req.ticket()))
        {
            response.set_error_code(100);
            response.set_error_message("Ticket not found");

            co_await detail::reply_message(socket, Method::ClientConnection, response);
            co_return;
        }

        response.set_error_code(0);
        response.set_error_message("success");

        co_await detail::reply_message(socket, Method::ClientConnection, response);
        sessions_[req.ticket()].tcp_socket_ = std::make_unique<net::ip::tcp::socket>(std::move(socket));
    }

    net::awaitable<void> service::online_notify(const std::string &ticket, const std::string &username, uint64_t id)
    {
        OnlineNotifyRequest req;
        req.set_username(username);
        req.set_id(id);

        for (auto &session : sessions_)
        {
            if (session.first != ticket)
            {
                co_await detail::reply_message(*session.second.tcp_socket_, Method::OnlineNotify, req);
            }
        }
    }

}