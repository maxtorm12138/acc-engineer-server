#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

#include <unordered_map>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/local/stream_protocol.hpp>


#include "rpc/stub.h"
#include "config.h"

#include "proto/service.pb.h"

namespace acc_engineer
{
    namespace net = boost::asio;

    using tcp_stub_t = rpc::stub<net::ip::tcp::socket>;
    using unix_socket_t = net::local::stream_protocol::socket;
    using unix_domain_stub_t = rpc::stub<unix_socket_t>;

    class service
    {
    public:
        service(config cfg);

        net::awaitable<void> run();

    private:
        net::awaitable<void> tcp_run();

        net::awaitable<void> udp_read_run();

        net::awaitable<void> udp_write_run(
                net::ip::udp::socket &acceptor,
                net::ip::udp::endpoint remote, std::shared_ptr<unix_socket_t> unix_socket);

        net::awaitable<void> unix_domain_run();


        net::awaitable<void> new_tcp_connection(net::ip::tcp::socket socket);

        net::awaitable<void> new_unix_domain_connection(unix_socket_t socket);

        net::awaitable<Echo::Response> echo(const Echo::Request &request);
        //net::awaitable<rpc::result<Authentication::Response>> authentication(const Authentication::Request &request);

        config config_;
        bool running_{false};
        rpc::method_group method_group_;

        std::unordered_map<uint64_t, std::shared_ptr<tcp_stub_t>> id_tcp_stub_;
        std::unordered_map<uint64_t, std::shared_ptr<unix_domain_stub_t>> id_unix_domain_stub_;
        std::unordered_map<net::ip::udp::endpoint, std::shared_ptr<unix_socket_t>> ep_unix_socket_;
    };
}

template<>
inline size_t
std::hash<boost::asio::ip::udp::endpoint>::operator()(const boost::asio::ip::udp::endpoint &ep) const noexcept
{
    return 1;
}

#endif //ACC_ENGINEER_SERVER_SERVICE_H
