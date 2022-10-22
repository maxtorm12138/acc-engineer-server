#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_H

#include <span>
#include <bitset>
#include <numeric>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/cancellation_condition.hpp>

#include "method.h"
#include "await_error_code.h"

#include "proto/rpc.pb.h"

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;
namespace sys = boost::system;

enum class stub_status
{
    idle,
    running,
    stopping,
    stopped
};

enum flags
{
    flag_is_request = 0,
    flag_no_reply = 1,
};

constexpr size_t MAX_IO_CHANNEL_BUFFER_SIZE = 100;
constexpr size_t MAX_WORKER_SIZE = 10;

template<typename PacketHandler>
class stub
{
public:
    using method_channel_type = typename PacketHandler::method_channel_type;
    using input_channel_type = net::experimental::channel<void(sys::error_code, std::vector<uint8_t>)>;
    using output_channel_type = net::experimental::channel<void(sys::error_code, std::vector<uint8_t>)>;
    using calling_channel_type = net::experimental::channel<void(sys::error_code, std::vector<uint8_t>)>;

    explicit stub(method_channel_type method_channel, const methods &methods = methods::empty());

    net::awaitable<void> run();

    net::awaitable<void> stop();

    net::awaitable<void> deliver(std::vector<uint8_t> packet);

private:
    net::awaitable<void> input_loop();

    net::awaitable<void> output_loop();

    template<size_t... WorkerIds>
    net::awaitable<void> spawn_worker_loop(std::index_sequence<WorkerIds...>);

    template<size_t WorkerId>
    net::awaitable<void> worker_loop();

    std::tuple<uint64_t, std::bitset<64>, rpc::Cookie, std::span<uint8_t>> unpack(const std::vector<uint8_t> &receive_buffer);
    std::vector<uint8_t> pack(uint64_t command_id, std::bitset<64> flags, rpc::Cookie cookie, std::vector<uint8_t> response_payload);

private:
    method_channel_type method_channel_;
    input_channel_type input_channel_;
    output_channel_type output_channel_;
    std::unordered_map<uint64_t, std::optional<calling_channel_type>> calling_channel_;
    stub_status status_;
    const methods &methods_;
    const uint64_t id_;

private:
    static std::atomic<uint64_t> stub_id_max_;
    static std::atomic<uint64_t> trace_id_max_;
};

template<typename PacketHandler>
stub<PacketHandler>::stub(method_channel_type method_channel, const methods &methods)
    : method_channel_(std::move(method_channel_))
    , input_channel_(method_channel_.get_executor(), MAX_IO_CHANNEL_BUFFER_SIZE)
    , output_channel_(method_channel_.get_executor(), MAX_IO_CHANNEL_BUFFER_SIZE)
    , status_(stub_status::idle)
    , methods_(methods)
    , id_(stub_id_max_++)
{}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::run()
{
    auto executor = co_await net::this_coro::executor;
    auto loop0 = net::co_spawn(executor, input_loop(), net::detached);
    auto loop1 = net::co_spawn(executor, output_loop(), net::detached);
    auto loop2 = net::co_spawn(executor, spawn_worker_loop(std::make_index_sequence<MAX_WORKER_SIZE>()), net::detached);

    auto run = net::experimental::make_parallel_group(std::move(loop0), std::move(loop1), std::move(loop2));

    auto result = co_await run.async_wait(net::experimental::wait_for_one(), net::deferred);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::stop()
{
    if (status_ == stub_status::running)
    {
        status_ = stub_status::stopping;
        method_channel_.cancel();
        input_channel_.cancel();
        output_channel_.cancel();
        for (auto &[trace_id, channel] : calling_channel_)
        {
            channel->cancel();
        }
    }
    co_return;
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::deliver(std::vector<uint8_t> packet)
{
    sys::error_code error_code;
    co_await input_channel_.async_send({}, packet, await_error_code(error_code));
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::input_loop()
{
    std::vector<uint8_t> receive_buffer(PacketHandler::MAX_PACKET_SIZE);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        error_code = co_await PacketHandler::receive_packet(method_channel_, receive_buffer);
        if (error_code == system_error::connection_closed)
        {
            throw sys::system_error(error_code);
        }

        if (error_code == system_error::operation_canceled)
        {
            co_return;
        }

        if (error_code)
        {
            throw sys::system_error(system_error::unhandled_system_error);
        }

        co_await input_channel_.async_send({}, receive_buffer, await_error_code(error_code));
        if (error_code == net::experimental::error::channel_closed)
        {
            throw sys::system_error(system_error::connection_closed);
        }

        if (error_code == net::experimental::error::channel_cancelled)
        {
            co_return;
        }

        if (error_code)
        {
            throw sys::system_error(system_error::unhandled_system_error);
        }
    }
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::output_loop()
{
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        std::vector<uint8_t> send_buffer = co_await output_channel_.async_receive(await_error_code(error_code));
        if (error_code == net::experimental::error::channel_closed)
        {
            throw sys::system_error(system_error::connection_closed);
        }

        if (error_code == net::experimental::error::channel_cancelled)
        {
            co_return;
        }

        if (error_code)
        {
            throw sys::system_error(system_error::unhandled_system_error);
        }

        error_code = co_await PacketHandler::send_packet(method_channel_, std::move(send_buffer));
        if (error_code == system_error::connection_closed)
        {
            throw sys::system_error(error_code);
        }

        if (error_code == system_error::operation_canceled)
        {
            co_return;
        }

        if (error_code)
        {
            throw sys::system_error(system_error::unhandled_system_error);
        }
    }
}

template<typename PacketHandler>
template<size_t... WorkerIds>
net::awaitable<void> stub<PacketHandler>::spawn_worker_loop(std::index_sequence<WorkerIds...>)
{
    auto executor = co_await net::this_coro::executor;
    auto run = net::experimental::make_parallel_group(net::co_spawn(executor, worker_loop<WorkerIds>(), net::deferred)...);
    auto result = co_await run.async_wait(net::experimental::wait_for_all(), net::deferred);
    co_return;
}

template<typename PacketHandler>
template<size_t WorkerId>
net::awaitable<void> stub<PacketHandler>::worker_loop()
{
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        std::vector<uint8_t> receive_buffer = co_await input_channel_.async_receive(await_error_code(error_code));
        if (error_code == net::experimental::error::channel_closed)
        {
            throw sys::system_error(system_error::connection_closed);
        }

        if (error_code == net::experimental::error::channel_cancelled)
        {
            co_return;
        }

        if (error_code)
        {
            throw sys::system_error(system_error::unhandled_system_error);
        }

        auto [command_id, flags, cookie, payload] = unpack(receive_buffer);

        if (flags.test(flag_is_request))
        {
            const context_t context{.stub_id = id_, .packet_handler_type = PacketHandler::type};
            std::vector<uint8_t> response_payload;
            std::vector<uint8_t> send_buffer;
            try
            {
                response_payload = co_await methods_(command_id, context, payload);
                if (flags.test(flag_no_reply))
                {
                    continue;
                }

                std::bitset<64> response_flags;
                response_flags.set(flag_is_request, false);
                send_buffer = pack(command_id, response_flags, std::move(cookie), std::move(response_payload));
            }
            catch (sys::system_error &ex)
            {
                if (ex.code().category() == system_error_category())
                {
                    cookie.set_error_code(ex.code().value());
                }
                else
                {
                    cookie.set_error_code(static_cast<uint64_t>(system_error::unhandled_system_error));
                }
            }

            co_await output_channel_.async_send({}, std::move(send_buffer), await_error_code(error_code));
            if (error_code == net::experimental::error::channel_closed)
            {
                throw sys::system_error(system_error::connection_closed);
            }

            if (error_code == net::experimental::error::channel_cancelled)
            {
                co_return;
            }

            if (error_code)
            {
                throw sys::system_error(system_error::unhandled_system_error);
            }
        }
        else if (auto calling = calling_channel_.find(cookie.trace_id()); calling != calling_channel_.end())
        {
            co_await calling->second->async_send(static_cast<system_error>(cookie.error_code()), std::vector(payload.begin(), payload.end()), await_error_code(error_code));
            calling_channel_.erase(cookie.trace_id());
            if (error_code == net::experimental::error::channel_closed)
            {
                throw sys::system_error(system_error::connection_closed);
            }

            if (error_code == net::experimental::error::channel_cancelled)
            {
                co_return;
            }

            if (error_code)
            {
                throw sys::system_error(system_error::unhandled_system_error);
            }
        }
        else {}
    }
}

template<typename PacketHandler>
std::vector<uint8_t> stub<PacketHandler>::pack(uint64_t command_id, std::bitset<64> flags, rpc::Cookie cookie, std::vector<uint8_t> payload)
{
    std::vector<uint8_t> cookie_payload(cookie.ByteSizeLong());
    if (!cookie.SerializeToArray(cookie_payload.data(), cookie_payload.size()))
    {
        throw sys::system_error(system_error::proto_serialize_fail);
    }

    uint64_t cookie_payload_size = cookie_payload.size();
    uint64_t payload_size = payload.size();
    uint64_t bit_flags = flags.to_ullong();

    std::array buffer_sequence = {
        net::buffer(&command_id, sizeof(command_id)),                   /**/
        net::buffer(&bit_flags, sizeof(bit_flags)),                     /**/
        net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)), /**/
        net::buffer(&payload_size, sizeof(payload_size)),               /**/
        net::buffer(cookie_payload), net::buffer(payload)               /**/
    };

    size_t total = std::accumulate(buffer_sequence.begin(), buffer_sequence.end(), 0ULL, [](auto &&buffer, auto &&current) { return current + buffer.size(); });

    std::vector<uint8_t> packed(total);
    net::buffer_copy(net::buffer(packed), buffer_sequence, total);
    return packed;
}
} // namespace acc_engineer::rpc::detail

#endif