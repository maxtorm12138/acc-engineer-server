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
#include "batch_task.h"

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
class stub : public std::enable_shared_from_this<stub<PacketHandler>>, public boost::noncopyable
{
public:
    using method_channel_type = typename PacketHandler::method_channel_type;
    using input_channel_type = net::experimental::channel<void(sys::error_code, std::vector<uint8_t>)>;
    using output_channel_type = net::experimental::channel<void(sys::error_code, std::vector<uint8_t>)>;
    using calling_channel_type = net::experimental::channel<void(sys::error_code, std::vector<uint8_t>)>;

    static std::shared_ptr<stub<PacketHandler>> create(method_channel_type method_channel, const methods &methods = methods::empty());

    ~stub();

    net::awaitable<void> run();

    net::awaitable<void> stop();

    net::awaitable<void> deliver(std::vector<uint8_t> packet);

    template<is_method_message MethodMessage>
    net::awaitable<response_t<MethodMessage>> async_call(const request_t<MethodMessage> &request);

    uint64_t id() const noexcept;

private:
    explicit stub(method_channel_type method_channel, const methods &methods = methods::empty());

    net::awaitable<void> input_loop();

    net::awaitable<void> output_loop();

    net::awaitable<void> worker_loop(uint64_t worker_id);

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
    std::unique_ptr<batch_task<void>> runner_;

private:
    static std::atomic<uint64_t> stub_id_max_;
    static std::atomic<uint64_t> trace_id_max_;
};

template<typename PacketHandler>
std::shared_ptr<stub<PacketHandler>> stub<PacketHandler>::create(typename stub<PacketHandler>::method_channel_type method_channel, const methods &methods)
{
    return std::shared_ptr<stub<PacketHandler>>(new stub<PacketHandler>(std::move(method_channel), methods));
}

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
    SPDLOG_TRACE("~stub {}", id_);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::run()
{
    auto executor = co_await net::this_coro::executor;
    runner_ = std::make_unique<batch_task<void>>(executor);

    status_ = stub_status::running;
    BOOST_SCOPE_EXIT_ALL(&)
    {
        status_ = stub_status::stopped;
    };

    co_await runner_->add(input_loop());
    co_await runner_->add(output_loop());

    for (int i = 0; i < MAX_WORKER_SIZE; i++)
    {
        co_await runner_->add(worker_loop(i));
    }

    sys::error_code error_code;
    auto [order, exceptions] = co_await runner_->async_wait(error_code);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::stop()
{
    if (status_ == stub_status::running)
    {
        SPDLOG_TRACE("stub {} stopping", id_);
        status_ = stub_status::stopping;
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

    bool no_reply = MethodMessage::descriptor()->options().GetExtension(rpc::no_reply);
    uint64_t trace_id = trace_id_max_++;
    std::bitset<64> flags;
    flags.set(flag_is_request).set(flag_no_reply, no_reply);

    rpc::Cookie cookie;
    cookie.set_trace_id(trace_id);
    cookie.set_error_code(0);

    SPDLOG_TRACE("async_call {} method: {} trace_id: {} request: {}", id_, command_id, trace_id, request.ShortDebugString());

    auto packet = pack(command_id, flags, std::move(cookie), request);

    calling_channel_[trace_id].emplace(co_await net::this_coro::executor);
    BOOST_SCOPE_EXIT_ALL(&)
    {
        calling_channel_.erase(trace_id);
    };

    sys::error_code error_code;
    co_await output_channel_.async_send({}, std::move(packet), await_error_code(error_code));
    SPDLOG_TRACE("async_call {} output_channel: {}", id_, error_code.message());

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

    if (no_reply)
    {
        co_return response_t<MethodMessage>{};
    }

    auto response_payload = co_await calling_channel_[trace_id]->async_receive(await_error_code(error_code));
    SPDLOG_TRACE("async_call {} calling_channel: {}", id_, error_code.message());

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
    SPDLOG_TRACE("input_loop {} started", id_);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        error_code = co_await PacketHandler::receive_packet(method_channel_, receive_buffer);
        if (error_code)
        {
            if (error_code == system_error::operation_canceled)
            {
                SPDLOG_INFO("input_loop {} PacketHandler::receive_packet system_error: {}", id_, error_code.message());
            }
            else
            {
                SPDLOG_ERROR("input_loop {} PacketHandler::receive_packet system_error: {}", id_, error_code.message());
            }
            break;
        }

        co_await input_channel_.async_send({}, receive_buffer, await_error_code(error_code));
        if (error_code)
        {
            if (error_code == net::experimental::error::channel_cancelled)
            {
                SPDLOG_INFO("input_loop {} input_channel_.async_send system_error: {}", id_, error_code.message());
            }
            else
            {
                SPDLOG_ERROR("input_loop {} input_channel_.async_send system_error: {}", id_, error_code.message());
            }
            break;
        }
    }
    SPDLOG_TRACE("input_loop {} stopped", id_);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::output_loop()
{
    SPDLOG_TRACE("output_loop {} started", id_);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        std::vector<uint8_t> send_buffer = co_await output_channel_.async_receive(await_error_code(error_code));
        if (error_code)
        {
            if (error_code == net::experimental::error::channel_cancelled)
            {
                SPDLOG_INFO("output_loop {} output_channel_.async_receive system_error: {}", id_, error_code.message());
            }
            else
            {
                SPDLOG_ERROR("output_loop {} output_channel_.async_receive system_error: {}", id_, error_code.message());
            }
            break;
        }

        error_code = co_await PacketHandler::send_packet(method_channel_, std::move(send_buffer));
        if (error_code)
        {
            if (error_code == system_error::operation_canceled)
            {
                SPDLOG_INFO("output_loop {} PacketHandler::send_packet system_error: {}", id_, error_code.message());
            }
            else
            {
                SPDLOG_ERROR("output_loop {} PacketHandler::send_packet system_error: {}", id_, error_code.message());
            }
            break;
        }
    }
    SPDLOG_TRACE("output_loop {} stopped", id_);
}

template<typename PacketHandler>
net::awaitable<void> stub<PacketHandler>::worker_loop(uint64_t worker_id)
{
    SPDLOG_TRACE("worker_loop {}:{} started", id_, worker_id);
    while (status_ == stub_status::running)
    {
        sys::error_code error_code;
        std::vector<uint8_t> receive_buffer = co_await input_channel_.async_receive(await_error_code(error_code));
        SPDLOG_TRACE("worker_loop {}:{} input_channel_.async_receive size: {} error: {}", id_, worker_id, receive_buffer.size(), error_code.message());
        if (error_code)
        {
            if (error_code == net::experimental::error::channel_cancelled)
            {
                SPDLOG_INFO("worker_loop {}:{} input_channel_.async_receive system_error: {}", id_, worker_id, error_code.message());
            }
            else
            {
                SPDLOG_ERROR("worker_loop {}:{} input_channel_.async_receive system_error: {}", id_, worker_id, error_code.message());
            }
            break;
        }

        auto [command_id, flags, cookie, payload] = unpack(receive_buffer);
        SPDLOG_TRACE("worker_loop {}:{} unpack command_id: {}, flags: {:#x}, cookie: [{}]", id_, worker_id, command_id, flags.to_ullong(), cookie.ShortDebugString());

        if (flags.test(flag_is_request))
        {
            const context context{.stub_id = id_, .packet_handler_type = PacketHandler::type};
            std::vector<uint8_t> response_payload = co_await methods_(command_id, context, payload, error_code);
            if (flags.test(flag_no_reply))
            {
                continue;
            }

            std::bitset<64> response_flags;
            response_flags.set(flag_is_request, false);

            if (error_code)
            {
                SPDLOG_ERROR("worker_loop {}:{} dispatch_request system_error: {}", id_, worker_id, error_code.message());
                cookie.set_error_code(error_code.value());
            }

            std::vector<uint8_t> send_buffer = pack(command_id, response_flags, std::move(cookie), std::move(response_payload));
            SPDLOG_TRACE("worker_loop {}:{} pack send_buffer size: {}", id_, worker_id, send_buffer.size());

            co_await output_channel_.async_send({}, std::move(send_buffer), await_error_code(error_code));
            SPDLOG_TRACE("worker_loop {}:{} output_channel_.async_send error: {}", id_, worker_id, error_code.message());
            if (error_code)
            {
                if (error_code == net::experimental::error::channel_cancelled)
                {
                    SPDLOG_INFO("worker_loop {}:{} output_channel_.async_send system_error: {}", id_, worker_id, error_code.message());
                }
                else
                {
                    SPDLOG_ERROR("worker_loop {}:{} output_channel_.async_send system_error: {}", id_, worker_id, error_code.message());
                }
                break;
            }
        }
        else
        {
            auto it_calling = calling_channel_.find(cookie.trace_id());
            if (it_calling == calling_channel_.end())
            {
                SPDLOG_INFO("worker_loop {}:{} message outdated", id_, worker_id);
                continue;
            }

            co_await it_calling->second->async_send(static_cast<system_error>(cookie.error_code()), std::vector(payload.begin(), payload.end()), await_error_code(error_code));
            SPDLOG_TRACE("worker_loop {}:{} calling_channel_.async_send error: {}", id_, worker_id, error_code.message());
            if (error_code)
            {
                if (error_code == net::experimental::error::channel_cancelled)
                {
                    SPDLOG_INFO("worker_loop {}:{} calling_channel_.async_send system_error: {}", id_, worker_id, error_code.message());
                }
                else
                {
                    SPDLOG_ERROR("worker_loop {}:{} calling_channel_.async_send system_error: {}", id_, worker_id, error_code.message());
                }
                break;
            }
        }
    }
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
std::atomic<uint64_t> stub<PacketHandler>::stub_id_max_{1};

template<typename PacketHandler>
std::atomic<uint64_t> stub<PacketHandler>::trace_id_max_{1};

} // namespace acc_engineer::rpc::detail

#endif