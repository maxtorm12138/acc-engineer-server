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
namespace sys = boost::system;

class service : public std::enable_shared_from_this<service>
{
public:
    explicit service(config cfg);

    net::awaitable<void> run();

    net::awaitable<void> stop();

private:
    net::awaitable<sys::error_code> timer_reset(uint64_t command_id, const rpc::context &context, google::protobuf::Message &);

    net::awaitable<Echo::Response> echo(const rpc::context &context, const Echo::Request &request);

    net::awaitable<Authentication::Response> authentication(const rpc::context &context, const Authentication::Request &request);

private:
    net::awaitable<void> tcp_run();

    net::awaitable<void> udp_run();

    net::awaitable<void> new_tcp_connection(net::ip::tcp::socket socket);

    net::awaitable<void> new_udp_connection(net::ip::udp::socket socket, std::vector<uint8_t> initial);

    template<typename Message>
    net::awaitable<void> post_tcp(const rpc::request_t<Message> &request);

    config config_;
    bool running_{false};
    rpc::methods methods_;

    std::unordered_map<uint64_t, std::weak_ptr<rpc::tcp_stub>> tcp_by_id_;
    std::unordered_map<uint64_t, std::weak_ptr<rpc::udp_stub>> udp_by_id_;
    std::unordered_map<net::ip::udp::endpoint, std::weak_ptr<rpc::udp_stub>> udp_by_endpoint_;
    std::unordered_map<uint64_t, std::weak_ptr<net::steady_timer>> timer_by_id_;

    std::unordered_map<uint64_t, std::weak_ptr<rpc::tcp_stub>> tcp_by_driver_id_;
    std::unordered_map<uint64_t, std::weak_ptr<rpc::udp_stub>> udp_by_driver_id_;

    std::unordered_map<std::string, uint64_t> driver_by_name_;

private:
    static std::atomic<uint64_t> driver_id_max_;
};

template<typename Message>
net::awaitable<void> service::post_tcp(const rpc::request_t<Message> &request)
{
    std::vector<uint64_t> error;
    for (auto [driver_id, weak_tcp_stub] : tcp_by_driver_id_)
    {
        if (auto tcp_stub = weak_tcp_stub.lock(); tcp_stub != nullptr)
        {
            co_await tcp_stub->async_call<Message>(request);
        }
    }
}
} // namespace acc_engineer

#endif // ACC_ENGINEER_SERVER_SERVICE_H
