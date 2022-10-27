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

        if (auto it_session = udp_sessions_.get<tag_udp_endpoint>().find(remote); it_session != udp_sessions_.get<tag_udp_endpoint>().end())
        {
            if (auto udp_stub = it_session->stub.lock(); udp_stub != nullptr)
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

    SPDLOG_INFO("{} tcp connected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
    try
    {
        auto tcp_stub = rpc::tcp_stub::create(std::move(socket), methods_);
        stub_id = tcp_stub->id();

        auto timer = std::make_shared<net::steady_timer>(co_await net::this_coro::executor);

        tcp_session session{.id = stub_id, .driver_id = 0, .stub = tcp_stub, .watcher = timer};
        staged_tcp_sessions_.emplace(std::move(session));

        BOOST_SCOPE_EXIT_ALL(&)
        {
            auto &view0 = staged_tcp_sessions_.get<tag_stub_id>();
            auto &view1 = tcp_sessions_.get<tag_stub_id>();

            if (auto it = view0.extract(stub_id); !it.empty())
            {
                SPDLOG_TRACE("new_tcp_connection extract {} from staged_tcp_sessions", stub_id);
            }
            else if (auto it = view1.extract(stub_id); !it.empty())
            {
                SPDLOG_TRACE("new_tcp_connection extract {} from tcp_sessions", stub_id);
            }
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

    SPDLOG_INFO("{} udp connected {}:{}", stub_id, remote_endpoint.address().to_string(), remote_endpoint.port());
    try
    {
        auto udp_stub = rpc::udp_stub::create(std::move(socket), methods_);
        stub_id = udp_stub->id();

        auto timer = std::make_shared<net::steady_timer>(co_await net::this_coro::executor);

        udp_session session{.id = stub_id, .driver_id = 0, .stub = udp_stub, .watcher = timer};

        staged_udp_sessions_.emplace(std::move(session));
        BOOST_SCOPE_EXIT_ALL(&)
        {
            auto &view0 = staged_udp_sessions_.get<tag_stub_id>();
            auto &view1 = udp_sessions_.get<tag_stub_id>();

            if (auto it = view0.extract(stub_id); !it.empty())
            {
                SPDLOG_DEBUG("new_udp_connection extract {} from staged_udp_sessions", stub_id);
            }
            else if (auto it = view1.extract(stub_id); !it.empty())
            {
                SPDLOG_DEBUG("new_udp_connection extract {} from udp_sessions", stub_id);
            }
        };

        auto watcher = [this, that = shared_from_this(), timer, weak_udp_stub = std::weak_ptr(udp_stub), stub_id]() -> net::awaitable<void> {
            sys::error_code error_code;
            timer->expires_after(10s);
            do
            {
                co_await timer->async_wait(rpc::await_error_code(error_code));
            } while (error_code == net::error::operation_aborted);

            SPDLOG_DEBUG("watcher {} expire trigger", stub_id);
            co_await weak_udp_stub.lock()->stop();
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

    uint64_t allocated_driver_id = 0;
    if (auto it_driver = driver_by_name_.find(request.driver_name()); it_driver != driver_by_name_.end() && request.driver_id() != 0)
    {
        if (it_driver->second != request.driver_id())
        {
            Authentication::Response response;
            response.set_error_code(2);
            response.set_error_message("authentication failure");
            co_return response;
        }

        allocated_driver_id = request.driver_id();
    }
    else
    {
        allocated_driver_id = driver_id_max_++;
        driver_by_name_[request.driver_name()] = allocated_driver_id;
    }

    if (context.packet_handler_type == rpc::tcp_packet_handler::type)
    {
        tcp_by_driver_id_[allocated_driver_id] = tcp_by_id_[context.stub_id];
    }
    else if (context.packet_handler_type == rpc::udp_packet_handler::type)
    {
        udp_by_driver_id_[allocated_driver_id] = udp_by_id_[context.stub_id];
    }

    DriverUpdate::Request driver_update_request;
    for (const auto &[driver_name, driver_id] : driver_by_name_)
    {
        auto driver = driver_update_request.add_drivers();
        driver->set_driver_name(driver_name);
        driver->set_driver_id(driver_id);
    }

    co_await post_tcp<DriverUpdate>(driver_update_request);

    Authentication::Response response;
    response.set_error_code(0);
    response.set_error_message("success");
    response.set_driver_id(allocated_driver_id);
    co_return response;
}

std::atomic<uint64_t> service::driver_id_max_{1};

} // namespace acc_engineer
