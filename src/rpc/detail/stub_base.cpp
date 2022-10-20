#include "stub_base.h"

// std
#include <numeric>

// boost
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

// spdlog
#include <spdlog/spdlog.h>

// module
#include "error_code.h"

namespace acc_engineer::rpc::detail {
std::atomic<uint64_t> stub_base::stub_id_max_;
std::atomic<uint64_t> stub_base::trace_id_max_;

stub_base::stub_base(const method_group &method_group)
    : method_group_(method_group)
{}

uint64_t stub_base::id() const
{
    return stub_id_;
}

stub_status stub_base::status() const
{
    return status_;
}

std::string stub_base::pack(uint64_t command_id, std::bitset<64> bit_flags, const Cookie &cookie, const std::string &message_payload) const
{
    uint64_t flags = bit_flags.to_ullong();
    std::string cookie_payload;
    if (!cookie.SerializeToString(&cookie_payload))
    {
        throw sys::system_error(system_error::proto_serialize_fail);
    }

    uint64_t payload_length = 0;
    uint64_t cookie_payload_size = cookie_payload.size();
    uint64_t message_payload_size = message_payload.size();

    const std::array<net::const_buffer, 7> payloads{net::buffer(&payload_length, sizeof(payload_length)), net::buffer(&command_id, sizeof(command_id)),
        net::buffer(&flags, sizeof(flags)), net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)), net::buffer(&message_payload_size, sizeof(message_payload_size)),
        net::buffer(cookie_payload), net::buffer(message_payload)};

    payload_length = std::accumulate(payloads.begin(), payloads.end(), 0ULL, [](auto current, auto &buffer) { return current + buffer.size(); }) - sizeof(payload_length);

    std::string result_payloads(payload_length + sizeof(payload_length), '\0');
    buffer_copy(net::buffer(result_payloads), payloads, result_payloads.size());

    return result_payloads;
}

std::tuple<uint64_t, std::bitset<64>, Cookie, std::string> stub_base::unpack(net::const_buffer payload) const
{
    if (payload.size() > MAX_PAYLOAD_SIZE)
    {
        throw sys::system_error(system_error::data_corrupted);
    }

    uint64_t payload_size = 0;
    uint64_t command_id = 0;
    uint64_t flags = 0;
    uint64_t cookie_payload_size = 0;
    uint64_t message_payload_size = 0;
    Cookie cookie{};
    std::string message_payload;

    const std::array header_payload{
        net::buffer(&payload_size, sizeof(payload_size)),
        net::buffer(&command_id, sizeof(command_id)),
        net::buffer(&flags, sizeof(flags)),
        net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
        net::buffer(&message_payload_size, sizeof(message_payload_size)),
    };

    buffer_copy(header_payload, payload, sizeof(uint64_t) * header_payload.size());
    std::bitset<64> bit_flags = flags;
    payload += sizeof(uint64_t) * header_payload.size();

    if (!cookie.ParseFromArray(payload.data(), static_cast<int>(cookie_payload_size)))
    {
        throw sys::system_error(system_error::data_corrupted);
    }
    payload += cookie_payload_size;

    message_payload.resize(message_payload_size);
    buffer_copy(net::buffer(message_payload), payload, message_payload_size);
    payload += message_payload_size;

    if (payload.size() != 0)
    {
        throw sys::system_error(system_error::data_corrupted);
    }

    return {command_id, bit_flags, std::move(cookie), std::move(message_payload)};
}

uint64_t stub_base::generate_trace_id()
{
    return trace_id_max_++;
}

net::awaitable<void> stub_base::dispatch(sender_channel_t &sender_channel, net::const_buffer payload)
{
    auto [command_id, bit_flags, cookie, message_payload] = unpack(payload);

    if (bit_flags.test(flag_is_request))
    {
        co_spawn(co_await net::this_coro::executor, invoke_method(sender_channel, command_id, bit_flags, std::move(cookie), std::move(message_payload)), net::detached);
    }
    else
    {
        uint64_t trace_id = cookie.trace_id();

        if (!calling_.contains(trace_id))
        {
            spdlog::info("{} dispatch_response message out of date, cmd_id: {} cookie: {}", id(), command_id, cookie.ShortDebugString());
            co_return;
        }

        co_await calling_[trace_id]->async_send({}, std::move(cookie), std::move(message_payload), net::use_awaitable);
    }
}

net::awaitable<void> stub_base::invoke_method(sender_channel_t &sender_channel, uint64_t command_id, std::bitset<64> bit_flags, Cookie cookie, std::string message_payload)
{
    spdlog::debug("{} invoke_method, cmd_id: {}, flags: {} cookie: {}", id(), command_id, bit_flags.to_string(), cookie.ShortDebugString());
    context_t context{.stub_id = id()};
    std::string response_message_payload;
    try
    {
        response_message_payload = co_await std::invoke(method_group_, command_id, std::move(context), std::move(message_payload));
    }
    catch (const sys::system_error &e)
    {
        spdlog::error("{} invoke_method system error, code: {} what: \"{}\"", id(), e.code().value(), e.what());
        cookie.set_error_code(e.code().value());
    }
    catch (const std::exception &e)
    {
        spdlog::error("{} invoke_method exception, what: \"{}\"", id(), e.what());
        cookie.set_error_code(static_cast<int>(system_error::unhandled_exception));
    }

    if (bit_flags.test(flag_no_reply))
    {
        co_return;
    }

    try
    {
        const auto flags = std::bitset<64>{}.set(flag_is_request, false).to_ullong();
        auto payloads = pack(command_id, flags, cookie, response_message_payload);
        co_await sender_channel.async_send({}, std::move(payloads), net::use_awaitable);
    }
    catch (const sys::system_error &e)
    {
        spdlog::error("{} invoke_method reply system error, code: {} what: \"{}\"", id(), e.code().value(), e.what());
    }
    catch (const std::exception &e)
    {
        spdlog::error("{} invoke_method reply exception, what: {}", id(), e.what());
    }
}
} // namespace acc_engineer::rpc::detail
