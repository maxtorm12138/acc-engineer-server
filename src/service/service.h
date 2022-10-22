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
#include "rpc/types.h"
#include "rpc/method_group.h"

#include "config.h"

// protocol
#include "proto/service.pb.h"

namespace acc_engineer {
namespace net = boost::asio;

using tcp_stub_t = rpc::stream_stub<net::ip::tcp::socket>;
using udp_stub_t = rpc::datagram_stub<net::ip::udp::socket>;

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

    net::awaitable<void> new_udp_connection(net::ip::udp::socket socket, std::string initial);

    void reset_watcher(uint64_t stub_id);

    config config_;
    bool running_{false};
    rpc::method_group method_group_;

    std::unordered_map<uint64_t, std::weak_ptr<tcp_stub_t>> id_tcp_stub_;
    std::unordered_map<uint64_t, std::weak_ptr<udp_stub_t>> id_udp_stub_;
    std::unordered_map<net::ip::udp::endpoint, uint64_t> ep_id_udp_;
    std::unordered_map<uint64_t, std::weak_ptr<net::steady_timer>> stub_watcher_;

    // driver
    std::unordered_map<std::string, uint64_t> driver_name_id_;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> driver_id_stub_;

    static std::atomic<uint64_t> driver_id_max_;
};
} // namespace acc_engineer

#endif // ACC_ENGINEER_SERVER_SERVICE_H
