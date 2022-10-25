#include "service.h"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/cancellation_condition.hpp>

#include "rpc/error_code.h"

#include "proto/service.pb.h"

namespace acc_engineer {

namespace sys = boost::system;

service::service(config cfg)
    : config_(std::move(cfg))
{}

net::awaitable<void> service::run()
{
    using namespace std::placeholders;
    running_ = true;
    methods_.add_request_interceptor(std::bind(&service::timer_reset, this, _1, _2, _3));

    methods_.implement<Echo>(std::bind(&service::echo, this, _1, _2));
    methods_.implement<Authentication>(std::bind(&service::authentication, this, _1, _2));

    auto executor = co_await net::this_coro::executor;
    auto loop0 = net::co_spawn(executor, udp_run(), net::deferred);
    auto loop1 = net::co_spawn(executor, tcp_run(), net::deferred);

    auto runner = net::experimental::make_parallel_group(std::move(loop0), std::move(loop1));
    auto result = co_await runner.async_wait(net::experimental::wait_for_one(), net::deferred);
}

net::awaitable<void> service::tcp_run()
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::acceptor acceptor(executor, net::ip::tcp::endpoint{config_.address(), config_.port()});
    SPDLOG_INFO("tcp_run listening on {}:{}", acceptor.local_endpoint().address().to_string(), acceptor.local_endpoint().port());
    while (running_)
    {
        auto socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(executor, new_tcp_connection(std::move(socket)), net::detached);
    }
    SPDLOG_INFO("tcp_run stopped");
}

net::awaitable<void> service::udp_run()
{
    auto executor = co_await net::this_coro::executor;

    net::ip::udp::endpoint bind_endpoint{config_.address(), config_.port()};
    net::ip::udp::socket acceptor(co_await net::this_coro::executor);
    acceptor.open(bind_endpoint.protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(bind_endpoint);

    SPDLOG_INFO("udp run listening on {}:{}", acceptor.local_endpoint().address().to_string(), acceptor.local_endpoint().port());
    std::vector<uint8_t> initial(1500);
    while (running_)
    {
        net::ip::udp::endpoint remote;
        size_t size_read = co_await acceptor.async_receive_from(net::buffer(initial), remote, net::use_awaitable);

        if (auto it_udp = udp_by_endpoint_.find(remote); it_udp != udp_by_endpoint_.end())
        {
            if (auto udp_stub = it_udp->second.lock(); udp_stub != nullptr)
            {
                co_await udp_stub->deliver(std::vector(initial.begin(), initial.begin() + size_read));
            }

            continue;
        }

        net::ip::udp::socket socket(co_await net::this_coro::executor);
        socket.open(bind_endpoint.protocol());
        socket.set_option(net::socket_base::reuse_address(true));
        socket.bind(bind_endpoint);
        socket.connect(remote);

        net::co_spawn(executor, new_udp_connection(std::move(socket), std::vector(initial.begin(), initial.begin() + size_read)), net::detached);
    }
    SPDLOG_INFO("udp_run stopped");
}

net::awaitable<void> service::new_tcp_connection(net::ip::tcp::socket socket)
{
    uint64_t stub_id = 0;
    auto remote_endpoint = socket.remote_endpoint();
    using namespace std::chrono_literals;

    spdlog::info("{} tcp connected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
    try
    {
        auto tcp_stub = rpc::tcp_stub::create(std::move(socket), methods_);
        auto timer = std::make_shared<net::steady_timer>(co_await net::this_coro::executor);
        stub_id = tcp_stub->id();

        tcp_by_id_.emplace(stub_id, tcp_stub);
        timer_by_id_.emplace(stub_id, timer);
        BOOST_SCOPE_EXIT_ALL(&)
        {
            timer_by_id_.erase(stub_id);
            tcp_by_id_.erase(stub_id);
        };

        auto watcher = [this, that = shared_from_this(), timer, tcp_stub, stub_id]() -> net::awaitable<void> {
            sys::error_code error_code;
            timer->expires_after(10s);
            do
            {
                co_await timer->async_wait(rpc::await_error_code(error_code));
            } while (error_code == net::error::operation_aborted);
            SPDLOG_DEBUG("watcher {} expire trigger", stub_id);
            co_await tcp_stub->stop();
        };

        net::co_spawn(co_await net::this_coro::executor, watcher(), net::detached);
        co_await tcp_stub->run();
    }
    catch (sys::system_error &ex)
    {
        spdlog::error("{} tcp run exception: {}", stub_id, ex.what());
    }
    spdlog::info("{} tcp disconnected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
}

net::awaitable<void> service::new_udp_connection(net::ip::udp::socket socket, std::vector<uint8_t> initial)
{
    uint64_t stub_id = 0;
    auto remote_endpoint = socket.remote_endpoint();
    using namespace std::chrono_literals;

    spdlog::info("{} udp connected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
    try
    {
        auto udp_stub = rpc::udp_stub::create(std::move(socket), methods_);
        auto timer = std::make_shared<net::steady_timer>(co_await net::this_coro::executor);
        stub_id = udp_stub->id();
        udp_by_id_.emplace(stub_id, udp_stub);
        udp_by_endpoint_.emplace(remote_endpoint, udp_stub);
        timer_by_id_.emplace(stub_id, timer);
        BOOST_SCOPE_EXIT_ALL(&)
        {
            timer_by_id_.erase(stub_id);
            udp_by_endpoint_.erase(remote_endpoint);
            udp_by_id_.erase(stub_id);
        };

        auto watcher = [this, that = shared_from_this(), timer, udp_stub, stub_id]() -> net::awaitable<void> {
            sys::error_code error_code;
            timer->expires_after(10s);
            do
            {
                co_await timer->async_wait(rpc::await_error_code(error_code));
            } while (error_code == net::error::operation_aborted);
            SPDLOG_DEBUG("watcher {} expire trigger", stub_id);
            co_await udp_stub->stop();
        };

        net::co_spawn(co_await net::this_coro::executor, watcher(), net::detached);
        co_await udp_stub->deliver(std::move(initial));
        co_await udp_stub->run();
    }
    catch (sys::system_error &ex)
    {
        spdlog::error("{} udp run exception: {}", stub_id, ex.what());
    }
    spdlog::info("{} udp disconnected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
}

net::awaitable<sys::error_code> service::timer_reset(uint64_t command_id, const rpc::context &context, google::protobuf::Message &)
{
    using namespace std::chrono_literals;
    if (auto it_timer = timer_by_id_.find(context.stub_id); it_timer != timer_by_id_.end())
    {
        if (auto timer = it_timer->second.lock(); timer != nullptr)
        {
            SPDLOG_DEBUG("timer_reset {} for {}", command_id, context.stub_id);
            timer->expires_after(10s);
        }
    }

    co_return rpc::system_error::success;
}

net::awaitable<Echo::Response> service::echo(const rpc::context &context, const Echo::Request &request)
{
    Echo::Response response;
    response.set_message(request.message());
    co_return response;
}

net::awaitable<Authentication::Response> service::authentication(const rpc::context &context, const Authentication::Request &request)
{

    if (request.password() != config_.password())
    {
        Authentication::Response response;
        response.set_error_code(1);
        response.set_error_message("authentication failure");
        co_return response;
    }

    /*
    if (driver_name_id_.contains(request.driver_name()) && driver_name_id_[request.driver_name()] == request.driver_id())
    {
        switch (context.stub_type)
        {
        case rpc::detail::stub_type::stream:
            driver_id_stub_[request.driver_id()].first = context.stub_id;
            break;
        case rpc::detail::stub_type::datagram:
            driver_id_stub_[request.driver_id()].second = context.stub_id;
            break;
        }
    }

    uint64_t allocated_driver_id = driver_id_max_++;

    for (auto &driver_item : driver_name_id_)
    {
        OnlineNotify::Request online_notify_request;
        online_notify_request.set_driver_id(allocated_driver_id);
        online_notify_request.set_driver_name(request.driver_name());

        uint64_t tcp_stub_id = driver_id_stub_[driver_item.second].first;

        if (auto stub = id_tcp_stub_[tcp_stub_id].lock(); stub != nullptr)
        {
            auto resp = co_await stub->async_call<OnlineNotify>(online_notify_request);
        }
    }

    driver_name_id_[request.driver_name()] = allocated_driver_id;
        */
    Authentication::Response response;
    response.set_error_code(0);
    response.set_error_message("success");
    response.set_driver_id(0);
    co_return response;
}

} // namespace acc_engineer
