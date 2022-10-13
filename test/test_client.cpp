#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/experimental/channel.hpp>
#include "service.pb.h"
#include "config.h"

namespace net = boost::asio;

template<typename Message>
net::awaitable<Message> fetch_message(net::ip::tcp::socket &socket, acc_engineer::Method method)
{
    uint16_t header[2];
    co_await net::async_read(socket, net::buffer(header), net::use_awaitable);
    if (header[0] != method)
    {
        throw std::runtime_error("method mismatch");
    }

    std::vector<uint8_t> buffer(header[1]);
    co_await net::async_read(socket, net::buffer(buffer), net::use_awaitable);

    Message message;
    message.ParseFromArray(buffer.data(), buffer.size());

    co_return message;
}

template<typename Message>
net::awaitable<void> reply_message(net::ip::tcp::socket &socket, acc_engineer::Method method, const Message &msg)
{
    uint16_t header[2];
    header[0] = method;
    header[1] = msg.ByteSizeLong();
    co_await net::async_write(socket, net::buffer(header), net::use_awaitable);

    auto data = msg.SerializeAsString();
    co_await net::async_write(socket, net::buffer(data), net::use_awaitable);
}

net::awaitable<void> handshake(acc_engineer::config config)
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::socket socket(executor);
    co_await socket.async_connect(net::ip::tcp::endpoint {config.address(), config.port()}, net::use_awaitable);

    acc_engineer::IssueTicketRequest request;
    request.set_username("maxtorm");
    request.set_password("123456");
    request.set_lobby("lobby");

    co_await reply_message(socket, acc_engineer::Method::IssueTicket, request);

    auto response = co_await fetch_message<acc_engineer::IssueTicketResponse>(socket, acc_engineer::Method::IssueTicket);

    std::cerr << "response: " << response.ShortDebugString() << std::endl;

    for (;;)
    {
        auto response = co_await fetch_message<acc_engineer::OnlineNotifyRequest>(socket, acc_engineer::Method::OnlineNotify);
        std::cerr << "response: " << response.ShortDebugString() << std::endl;
    }
}

int main(int argc, char *argv[])
{
    auto config = acc_engineer::config::from_command_line(argc, argv);
    net::io_context io_context;

    net::co_spawn(io_context, [config](){return handshake(config);}, net::detached);

    io_context.run();
}