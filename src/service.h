#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

#include <unordered_map>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#include "config.h"
#include "rpc/stub.h"

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
    std::unordered_map<uint64_t, std::tuple<uint64_t, uint64_t, std::string>> driver_stub_;
    std::unordered_map<uint64_t, std::weak_ptr<net::steady_timer>> stub_watcher_;

    static std::atomic<uint64_t> driver_id_max_;
};
} // namespace acc_engineer

#endif // ACC_ENGINEER_SERVER_SERVICE_H
