#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

// std
#include <unordered_map>

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

// module
#include "rpc/stub.h"
#include "rpc/method.h"

#include "config.h"

// protocol
#include "proto/service.pb.h"

namespace acc_engineer {
namespace net = boost::asio;

class service
{
public:
    explicit service(config cfg);

    net::awaitable<void> run();

private:
    net::awaitable<Echo::Response> echo(const rpc::context_t &context, const Echo::Request &request);

    net::awaitable<Authentication::Response> authentication(const rpc::context_t &context, const Authentication::Request &request);

    net::awaitable<void> tcp_run();

    net::awaitable<void> udp_run();

    net::awaitable<void> new_tcp_connection(net::ip::tcp::socket socket);

    net::awaitable<void> new_udp_connection(net::ip::udp::socket socket, std::vector<uint8_t> initial);

    config config_;
    bool running_{false};
    rpc::methods methods_;

    std::unordered_map<uint64_t, std::shared_ptr<rpc::tcp_stub>> conn_id_tcp_;
    std::unordered_map<uint64_t, std::shared_ptr<rpc::udp_stub>> conn_id_udp_;
    std::unordered_map<net::ip::udp::endpoint, uint64_t> conn_ep_udp_;
};
} // namespace acc_engineer

#endif // ACC_ENGINEER_SERVER_SERVICE_H
