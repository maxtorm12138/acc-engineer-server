#ifndef ACC_ENGINEER_SERVER_SERVICE_H
#define ACC_ENGINEER_SERVER_SERVICE_H

// std
#include <unordered_map>

// boost
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/key.hpp>

// module
#include "rpc/stub.h"
#include "rpc/method.h"
#include "rpc/batch_task.h"

#include "config.h"

// protocol
#include "proto/service.pb.h"

namespace acc_engineer {
namespace net = boost::asio;
namespace sys = boost::system;
namespace mi = boost::multi_index;

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

    struct udp_session
    {
        uint64_t id;
        uint64_t driver_id;
        net::ip::udp::endpoint endpoint;

        std::weak_ptr<rpc::udp_stub> stub;
        std::weak_ptr<net::steady_timer> watcher;
    };

    struct tcp_session
    {
        uint64_t id;
        uint64_t driver_id;

        std::weak_ptr<rpc::tcp_stub> stub;
        std::weak_ptr<net::steady_timer> watcher;
    };

    struct driver
    {
        uint64_t id;
        std::string name;
    };

    struct tag_stub_id
    {};

    struct tag_udp_endpoint
    {};

    struct tag_driver_id
    {};

    struct tag_driver_name
    {};

    // clang-format off
    boost::multi_index_container<
        udp_session,
        mi::indexed_by<
            mi::hashed_unique<mi::tag<tag_stub_id>, mi::key< &udp_session::id>>,
            mi::hashed_unique<mi::tag<tag_driver_id>, mi::key<&udp_session::driver_id>>,
            mi::hashed_unique<mi::tag<tag_udp_endpoint>, mi::key<&udp_session::endpoint>>
        >
    > udp_sessions_;

    boost::multi_index_container<
        udp_session,
        mi::indexed_by<
            mi::hashed_unique<mi::tag<tag_stub_id>, mi::key<&udp_session::id>>,
            mi::hashed_unique<mi::tag<tag_udp_endpoint>, mi::key<&udp_session::endpoint>>
        >
    > staged_udp_sessions_;

    boost::multi_index_container<
        tcp_session,
        mi::indexed_by<
            mi::hashed_unique<mi::tag<tag_stub_id>, mi::key<&tcp_session::id>>,
            mi::hashed_unique<mi::tag<tag_driver_id>, mi::key<&tcp_session::driver_id>>
        >
    > tcp_sessions_;

    boost::multi_index_container<
        tcp_session,
        mi::indexed_by<
            mi::hashed_unique<mi::tag<tag_stub_id>, mi::key<&tcp_session::id>>
        >
    > staged_tcp_sessions_;

    boost::multi_index_container<
        driver,
        mi::indexed_by<
            mi::hashed_unique< mi::tag<tag_driver_id>, mi::key<&driver::id>>,
            mi::hashed_unique< mi::tag<tag_driver_name>, mi::key<&driver::name>>
        >
    > drivers_;
    // clang-format on

private:
    static std::atomic<uint64_t> driver_id_max_;
};

template<typename Message>
net::awaitable<void> service::post_tcp(const rpc::request_t<Message> &request)
{
    auto executor = co_await net::this_coro::executor;
    rpc::batch_task<rpc::response_t<Message>> poster(executor);

    for (auto [driver_id, weak_tcp_stub] : tcp_by_driver_id_)
    {
        if (auto tcp_stub = weak_tcp_stub.lock(); tcp_stub != nullptr)
        {
            poster.add([tcp_stub, &request]() -> net::awaitable<rpc::response_t<Message>> { co_return co_await tcp_stub->async_call<Message>(request); });
        }
    }

    auto [order, exceptions, results] = co_await poster.async_wait();
}

} // namespace acc_engineer

#endif // ACC_ENGINEER_SERVER_SERVICE_H
