#include "service.h"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include "rpc/await_ec.h"
#include "proto/service.pb.h"

namespace acc_engineer {

namespace sys = boost::system;

std::atomic<uint64_t> service::driver_id_max_{1};

service::service(config cfg)
    : config_(std::move(cfg))
{}

net::awaitable<void> service::run()
{
    using namespace std::placeholders;
    using namespace net::experimental::awaitable_operators;
    running_ = true;
    method_group_.implement<Echo>(std::bind(&service::echo, this, _1, _2));
    method_group_.implement<Authentication>(std::bind(&service::authentication, this, _1, _2));

    co_await (tcp_run() && udp_run());
}

net::awaitable<void> service::tcp_run()
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::acceptor acceptor(executor, net::ip::tcp::endpoint{config_.address(), config_.port()});

    while (running_)
    {
        auto socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(executor, new_tcp_connection(std::move(socket)), net::detached);
    }
}

net::awaitable<void> service::udp_run()
{
    auto executor = co_await net::this_coro::executor;

    net::ip::udp::endpoint bind_endpoint{config_.address(), config_.port()};
    net::ip::udp::socket acceptor(co_await net::this_coro::executor);
    acceptor.open(bind_endpoint.protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(bind_endpoint);

    std::string initial(1500, '\0');
    while (running_)
    {
        net::ip::udp::endpoint remote;
        size_t size_read = co_await acceptor.async_receive_from(net::buffer(initial), remote, net::use_awaitable);

        net::ip::udp::socket socket(co_await net::this_coro::executor);
        socket.open(bind_endpoint.protocol());
        socket.set_option(net::socket_base::reuse_address(true));
        socket.bind(bind_endpoint);
        socket.connect(remote);

        net::co_spawn(executor, new_udp_connection(std::move(socket), std::string(initial.data(), size_read)), net::detached);
    }
}

net::awaitable<void> service::new_tcp_connection(net::ip::tcp::socket socket)
{
    uint64_t stub_id = 0;
    auto remote_endpoint = socket.remote_endpoint();
    using namespace std::chrono_literals;

    try
    {
        auto tcp_stub = std::make_shared<tcp_stub_t>(std::move(socket), method_group_);
        stub_id = tcp_stub->id();
        auto stub_watch = std::make_shared<net::steady_timer>(co_await net::this_coro::executor);
        auto watcher = [stub_id, weak_tcp_stub = std::weak_ptr<tcp_stub_t>(tcp_stub), stub_watch]() -> net::awaitable<void> {
            sys::error_code ec = net::error::operation_aborted;
            while (ec == net::error::operation_aborted)
            {
                co_await stub_watch->async_wait(rpc::await_ec[ec]);
            }

            spdlog::info("{} stub_watch timeout", stub_id);
            if (auto stub = weak_tcp_stub.lock())
            {
                co_await stub->stop();
            }
        };

        spdlog::info("{} tcp connected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());

        stub_watcher_[stub_id] = stub_watch;
        id_tcp_stub_.emplace(stub_id, tcp_stub);
        BOOST_SCOPE_EXIT_ALL(&)
        {
            stub_watcher_.erase(stub_id);
            id_tcp_stub_.erase(stub_id);
        };

        stub_watch->expires_after(10s);
        net::co_spawn(co_await net::this_coro::executor, watcher(), net::detached);
        co_await tcp_stub->run();
    }
    catch (sys::system_error &ex)
    {}
    spdlog::info("{} tcp disconnected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
}

net::awaitable<void> service::new_udp_connection(net::ip::udp::socket socket, std::string initial)
{
    uint64_t stub_id = 0;
    auto remote_endpoint = socket.remote_endpoint();
    using namespace std::chrono_literals;

    try
    {
        auto udp_stub = std::make_shared<udp_stub_t>(std::move(socket), method_group_);
        stub_id = udp_stub->id();
        auto stub_watch = std::make_shared<net::steady_timer>(co_await net::this_coro::executor);

        auto watcher = [stub_id, weak_udp_stub = std::weak_ptr<udp_stub_t>(udp_stub), stub_watch]() -> net::awaitable<void> {
            sys::error_code ec = net::error::operation_aborted;
            while (ec == net::error::operation_aborted)
            {
                co_await stub_watch->async_wait(rpc::await_ec[ec]);
            }

            spdlog::info("{} stub_watch timeout", stub_id);
            if (auto stub = weak_udp_stub.lock())
            {
                co_await stub->stop();
            }
            spdlog::info("{} stub_watch stopped", stub_id);
        };

        spdlog::info("{} udp connected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());

        stub_watcher_[stub_id] = stub_watch;
        id_udp_stub_[stub_id] = udp_stub;
        BOOST_SCOPE_EXIT_ALL(&)
        {
            stub_watcher_.erase(stub_id);
            id_udp_stub_.erase(stub_id);
        };

        stub_watch->expires_after(500ms);
        net::co_spawn(co_await net::this_coro::executor, watcher(), net::detached);
        co_await udp_stub->run(net::buffer(initial));
    }
    catch (sys::system_error &ex)
    {}
    spdlog::info("{} udp disconnected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
}

net::awaitable<Echo::Response> service::echo(const rpc::context_t &context, const Echo::Request &request)
{
    reset_watcher(context.stub_id);

    Echo::Response response;
    response.set_message(request.message());
    co_return response;
}

net::awaitable<Authentication::Response> service::authentication(const rpc::context_t &context, const Authentication::Request &request)
{
    reset_watcher(context.stub_id);

    Authentication::Response response;
    response.set_error_code(0);
    response.set_error_message("success");
    response.set_driver_id(driver_id_max_++);
    co_return response;
}

void service::reset_watcher(uint64_t stub_id)
{
    using namespace std::chrono_literals;
    if (auto watch = stub_watcher_[stub_id].lock(); watch != nullptr)
    {
        watch->expires_after(10s);
    }
}
} // namespace acc_engineer
