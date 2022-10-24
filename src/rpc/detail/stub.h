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

#include <boost/scope_exit.hpp>

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

    ~stub();

    net::awaitable<void> run();

    net::awaitable<void> stop();

    net::awaitable<void> deliver(std::vector<uint8_t> packet);

    template<is_method_message MethodMessage>
    net::awaitable<response_t<MethodMessage>> async_call(const request_t<MethodMessage> &request);

    uint64_t id() const noexcept;

private:
    net::awaitable<void> input_loop();

    net::awaitable<void> output_loop();

    template<size_t... WorkerIds>
    net::awaitable<void> spawn_worker_loop(std::index_sequence<WorkerIds...>);

    template<size_t WorkerId>
    net::awaitable<void> worker_loop();

    std::tuple<uint64_t, std::bitset<64>, rpc::Cookie, std::span<uint8_t>> unpack(const std::vector<uint8_t> &receive_buffer);

    std::vector<uint8_t> pack(uint64_t command_id, std::bitset<64> flags, rpc::Cookie cookie, std::vector<uint8_t> payload);

    std::vector<uint8_t> pack(uint64_t command_id, std::bitset<64> flags, rpc::Cookie cookie, const google::protobuf::Message &message);

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
    : method_channel_(std::move(method_channel))
    , input_channel_(method_channel_.get_executor(), MAX_IO_CHANNEL_BUFFER_SIZE)
    , output_channel_(method_channel_.get_executor(), MAX_IO_CHANNEL_BUFFER_SIZE)
    , status_(stub_status::idle)
    , methods_(methods)
    , id_(stub_id_max_++)
{}

template<typename PacketHandler>
stub<PacketHandler>::~stub()
{
    SPDLOG_DEBUG("~stub {}", id_);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::run()
{
    status_ = stub_status::running;
    BOOST_SCOPE_EXIT_ALL(&)
    {
        status_ = stub_status::stopped;
    };

    auto executor = co_await net::this_coro::executor;
    auto loop0 = net::co_spawn(executor, input_loop(), net::deferred);
    auto loop1 = net::co_spawn(executor, output_loop(), net::deferred);
    auto loop2 = net::co_spawn(executor, spawn_worker_loop(std::make_index_sequence<MAX_WORKER_SIZE>()), net::deferred);

    auto runner = net::experimental::make_parallel_group(std::move(loop0), std::move(loop1), std::move(loop2));

    auto result = co_await runner.async_wait(net::experimental::wait_for_one(), net::deferred);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::stop()
{
    if (status_ == stub_status::running)
    {
        SPDLOG_DEBUG("stub {} stopping", id_);
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
template<is_method_message MethodMessage>
net::awaitable<response_t<MethodMessage>> stub<PacketHandler>::async_call(const request_t<MethodMessage> &request)
{
    uint64_t command_id = MethodMessage::descriptor()->options().GetExtension(rpc::cmd_id);
    SPDLOG_DEBUG("async_call {} method: {} request: {}", id_, command_id, request.ShortDebugString());

    bool no_reply = MethodMessage::descriptor()->options().GetExtension(rpc::no_reply);
    uint64_t trace_id = trace_id_max_++;
    std::bitset<64> flags;
    flags.set(flag_is_request).set(flag_no_reply, no_reply);

    rpc::Cookie cookie;
    cookie.set_trace_id(trace_id);
    cookie.set_error_code(0);

    auto packet = pack(command_id, flags, std::move(cookie), request);

    calling_channel_[trace_id].emplace(co_await net::this_coro::executor);
    BOOST_SCOPE_EXIT_ALL(&)
    {
        calling_channel_.erase(trace_id);
    };

    sys::error_code error_code;
    co_await output_channel_.async_send({}, std::move(packet), await_error_code(error_code));
    SPDLOG_DEBUG("async_call {} output_channel: {}", id_, error_code.message());

    if (error_code == net::experimental::error::channel_closed)
    {
        throw sys::system_error(system_error::connection_closed);
    }

    if (error_code == net::experimental::error::channel_cancelled)
    {
        throw sys::system_error(system_error::connection_closed);
    }

    if (error_code)
    {
        throw sys::system_error(system_error::unhandled_system_error);
    }

    auto response_payload = co_await calling_channel_[trace_id]->async_receive(await_error_code(error_code));
    SPDLOG_DEBUG("async_call {} calling_channel: {}", id_, error_code.message());

    if (error_code == net::experimental::error::channel_closed)
    {
        throw sys::system_error(system_error::connection_closed);
    }

    if (error_code == net::experimental::error::channel_cancelled)
    {
        throw sys::system_error(system_error::connection_closed);
    }

    if (error_code && error_code.category() == system_error_category())
    {
        throw sys::system_error(error_code);
    }

    if (error_code)
    {
        throw sys::system_error(system_error::unhandled_system_error);
    }

    response_t<MethodMessage> response{};
    if (!response.ParseFromArray(response_payload.data(), static_cast<int>(response_payload.size())))
    {
        throw sys::system_error(system_error::proto_parse_fail);
    }

    co_return response;
}

template<typename PacketHandler>
uint64_t stub<PacketHandler>::id() const noexcept
{
    return id_;
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::input_loop()
{
    std::vector<uint8_t> receive_buffer(PacketHandler::MAX_PACKET_SIZE);
    SPDLOG_DEBUG("input_loop {} started", id_);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        error_code = co_await PacketHandler::receive_packet(method_channel_, receive_buffer);
        SPDLOG_DEBUG("input_loop {} receive_packet: {} ", id_, error_code.message());

        if (error_code == system_error::connection_closed)
        {
            SPDLOG_ERROR("input_loop {} receive_packet system_error: {}", id_, error_code.message());
            throw sys::system_error(error_code);
        }

        if (error_code == system_error::operation_canceled)
        {
            SPDLOG_DEBUG("input_loop {} receive_packet system_error: {}", id_, error_code.message());
            break;
        }

        if (error_code)
        {
            SPDLOG_ERROR("input_loop {} receive_packet system_error: {}", id_, error_code.message());
            throw sys::system_error(system_error::unhandled_system_error);
        }

        co_await input_channel_.async_send({}, receive_buffer, await_error_code(error_code));
        SPDLOG_DEBUG("input_loop {} input_channel: {} ", id_, error_code.message());

        if (error_code == net::experimental::error::channel_closed)
        {
            SPDLOG_ERROR("input_loop {} input_channel system_error: {}", id_, error_code.message());
            throw sys::system_error(system_error::connection_closed);
        }

        if (error_code == net::experimental::error::channel_cancelled)
        {
            SPDLOG_DEBUG("input_loop {} input_channel system_error: {}", id_, error_code.message());
            break;
        }

        if (error_code)
        {
            SPDLOG_ERROR("input_loop {} input_channel system_error: {}", id_, error_code.message());
            throw sys::system_error(system_error::unhandled_system_error);
        }
    }
    SPDLOG_DEBUG("input_loop {} stopped", id_);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::output_loop()
{
    SPDLOG_DEBUG("output_loop {} started", id_);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        std::vector<uint8_t> send_buffer = co_await output_channel_.async_receive(await_error_code(error_code));
        SPDLOG_DEBUG("output_loop {} output_channel: {} ", id_, error_code.message());

        if (error_code == net::experimental::error::channel_closed)
        {
            SPDLOG_ERROR("output_loop {} output_channel: {} ", id_, error_code.message());
            throw sys::system_error(system_error::connection_closed);
        }

        if (error_code == net::experimental::error::channel_cancelled)
        {
            SPDLOG_DEBUG("output_loop {} output_channel: {} ", id_, error_code.message());
            break;
        }

        if (error_code)
        {
            SPDLOG_ERROR("output_loop {} output_channel: {} ", id_, error_code.message());
            throw sys::system_error(system_error::unhandled_system_error);
        }

        error_code = co_await PacketHandler::send_packet(method_channel_, std::move(send_buffer));
        SPDLOG_DEBUG("output_loop {} send_packet: {} ", id_, error_code.message());

        if (error_code == system_error::connection_closed)
        {
            SPDLOG_ERROR("output_loop {} send_packet: {} ", id_, error_code.message());
            throw sys::system_error(error_code);
        }

        if (error_code == system_error::operation_canceled)
        {
            SPDLOG_DEBUG("output_loop {} send_packet: {} ", id_, error_code.message());
            break;
        }

        if (error_code)
        {
            SPDLOG_ERROR("output_loop {} send_packet: {} ", id_, error_code.message());
            throw sys::system_error(system_error::unhandled_system_error);
        }
    }
    SPDLOG_DEBUG("output_loop {} stopped", id_);
}

template<typename PacketHandler>
template<size_t... WorkerIds>
net::awaitable<void> stub<PacketHandler>::spawn_worker_loop(std::index_sequence<WorkerIds...>)
{
    auto executor = co_await net::this_coro::executor;
    auto run = net::experimental::make_parallel_group(net::co_spawn(executor, worker_loop<WorkerIds>(), net::deferred)...);
    auto result = co_await run.async_wait(net::experimental::wait_for_all(), net::deferred);
}

template<typename PacketHandler>
template<size_t WorkerId>
net::awaitable<void> stub<PacketHandler>::worker_loop()
{
    SPDLOG_DEBUG("worker_loop {} worker_id: {} started", id_, WorkerId);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        std::vector<uint8_t> receive_buffer = co_await input_channel_.async_receive(await_error_code(error_code));
        if (error_code == net::experimental::error::channel_closed)
        {
            SPDLOG_ERROR("worker_loop {} worker_id: {} input_channel system_error: {}", id_, WorkerId, error_code.message());
            throw sys::system_error(system_error::connection_closed);
        }

        if (error_code == net::experimental::error::channel_cancelled)
        {
            SPDLOG_DEBUG("worker_loop {} worker_id: {} input_channel system_error: {}", id_, WorkerId, error_code.message());
            break;
        }

        if (error_code)
        {
            SPDLOG_ERROR("worker_loop {} worker_id: {} input_channel system_error: {}", id_, WorkerId, error_code.message());
            throw sys::system_error(system_error::unhandled_system_error);
        }

        auto [command_id, flags, cookie, payload] = unpack(receive_buffer);

        if (flags.test(flag_is_request))
        {
            SPDLOG_DEBUG("worker_loop {} worker_id: {} command_id {} dispatch_request", id_, WorkerId, command_id);

            const context context{.stub_id = id_, .packet_handler_type = PacketHandler::type};
            std::vector<uint8_t> response_payload = co_await methods_(command_id, context, payload, error_code);
            if (flags.test(flag_no_reply))
            {
                SPDLOG_DEBUG("worker_loop {} worker_id: {} command_id {} dispatch_request no reply", id_, WorkerId, command_id);
                continue;
            }

            std::bitset<64> response_flags;
            response_flags.set(flag_is_request, false);

            if (error_code)
            {
                SPDLOG_INFO("worker_loop {} worker_id: {} command_id {} dispatch_request system_error: {}", id_, WorkerId, command_id, error_code.message());
                cookie.set_error_code(error_code.value());
            }

            std::vector<uint8_t> send_buffer = pack(command_id, response_flags, std::move(cookie), std::move(response_payload));

            co_await output_channel_.async_send({}, std::move(send_buffer), await_error_code(error_code));
            if (error_code == net::experimental::error::channel_closed)
            {
                SPDLOG_ERROR("worker_loop {} worker_id: {} dispatch_request output_channel system_error: {}", id_, WorkerId, error_code.message());
                throw sys::system_error(system_error::connection_closed);
            }

            if (error_code == net::experimental::error::channel_cancelled)
            {
                SPDLOG_DEBUG("worker_loop {} worker_id: {} dispatch request output_channel system_error: {}", id_, WorkerId, error_code.message());
                break;
            }

            if (error_code)
            {
                SPDLOG_ERROR("worker_loop {} worker_id: {} dispatch request output_channel system_error: {}", id_, WorkerId, error_code.message());
                throw sys::system_error(system_error::unhandled_system_error);
            }
        }
        else
        {
            SPDLOG_DEBUG("worker_loop {} worker_id: {} command_id {} dispatch response", id_, WorkerId, command_id);
            auto it_calling = calling_channel_.find(cookie.trace_id());
            if (it_calling == calling_channel_.end())
            {
                SPDLOG_INFO("worker_loop {} worker_id: {} command_id {} dispatch response outdated trace_id: {}", id_, WorkerId, command_id, cookie.trace_id());
                continue;
            }

            co_await it_calling->second->async_send(static_cast<system_error>(cookie.error_code()), std::vector(payload.begin(), payload.end()), await_error_code(error_code));
            if (error_code == net::experimental::error::channel_closed)
            {
                SPDLOG_ERROR("worker_loop {} worker_id: {} dispatch response output_channel system_error: {}", id_, WorkerId, error_code.message());
                throw sys::system_error(system_error::connection_closed);
            }

            if (error_code == net::experimental::error::channel_cancelled)
            {
                SPDLOG_DEBUG("worker_loop {} worker_id: {} dispatch response output_channel system_error: {}", id_, WorkerId, error_code.message());
                break;
            }

            if (error_code)
            {
                SPDLOG_ERROR("worker_loop {} worker_id: {} dispatch response output_channel system_error: {}", id_, WorkerId, error_code.message());
                throw sys::system_error(system_error::unhandled_system_error);
            }
        }
    }
    SPDLOG_DEBUG("worker_loop {} worker_id: {} stopped", id_, WorkerId);
}

template<typename PacketHandler>
std::tuple<uint64_t, std::bitset<64>, rpc::Cookie, std::span<uint8_t>> stub<PacketHandler>::unpack(const std::vector<uint8_t> &receive_buffer)
{
    uint64_t command_id;
    uint64_t bit_flags;
    uint64_t cookie_payload_size;
    uint64_t payload_size;

    std::array header = {
        net::buffer(&command_id, sizeof(command_id)),                   /**/
        net::buffer(&bit_flags, sizeof(bit_flags)),                     /**/
        net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)), /**/
        net::buffer(&payload_size, sizeof(payload_size)),               /**/
    };

    auto receive_buffer_view = net::buffer(receive_buffer);

    auto header_size = net::buffer_copy(header, receive_buffer_view);
    receive_buffer_view += header_size;

    rpc::Cookie cookie{};
    if (!cookie.ParseFromArray(receive_buffer_view.data(), static_cast<int>(cookie_payload_size)))
    {
        throw sys::system_error(system_error::proto_parse_fail);
    }

    receive_buffer_view += cookie_payload_size;

    std::span payload(static_cast<uint8_t *>(const_cast<void *>(receive_buffer_view.data())), payload_size);
    receive_buffer_view += payload_size;
    if (receive_buffer_view.size() != 0)
    {
        throw sys::system_error(system_error::data_corrupted);
    }

    return {command_id, std::bitset<64>(bit_flags), std::move(cookie), payload};
}

template<typename PacketHandler>
std::vector<uint8_t> stub<PacketHandler>::pack(uint64_t command_id, std::bitset<64> flags, rpc::Cookie cookie, std::vector<uint8_t> payload)
{
    std::vector<uint8_t> cookie_payload(cookie.ByteSizeLong());
    if (!cookie.SerializeToArray(cookie_payload.data(), static_cast<int>(cookie_payload.size())))
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

    size_t total = std::accumulate(buffer_sequence.begin(), buffer_sequence.end(), 0ULL, [](auto &&current, auto &&buffer) { return current + buffer.size(); });

    std::vector<uint8_t> packed(total);
    net::buffer_copy(net::buffer(packed), buffer_sequence, total);
    return packed;
}

template<typename PacketHandler>
std::vector<uint8_t> stub<PacketHandler>::pack(uint64_t command_id, std::bitset<64> flags, rpc::Cookie cookie, const google::protobuf::Message &message)
{
    std::vector<uint8_t> message_payload(message.ByteSizeLong());
    if (!message.SerializeToArray(message_payload.data(), static_cast<int>(message_payload.size())))
    {
        throw sys::system_error(system_error::proto_serialize_fail);
    }

    return pack(command_id, flags, std::move(cookie), std::move(message_payload));
}

template<typename PacketHandler>
std::atomic<uint64_t> stub<PacketHandler>::stub_id_max_;

template<typename PacketHandler>
std::atomic<uint64_t> stub<PacketHandler>::trace_id_max_;

} // namespace acc_engineer::rpc::detail

#endif