#include "stub_base.h"

// std
#include <numeric>

// boost
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

// spdlog
#include <spdlog/spdlog.h>

// module
#include "../result.h"
#include "error_code.h"

namespace acc_engineer::rpc::detail
{
    std::atomic<uint64_t> stub_base::stub_id_max_;
    std::atomic<uint64_t> stub_base::trace_id_max_;

    stub_base::stub_base(const method_group &method_group) : method_group_(method_group)
    {  }


    uint64_t stub_base::id() const
    {
        return stub_id_;
    }

    stub_status stub_base::status() const
    {
        return status_;
    }

    std::string stub_base::pack(uint64_t command_id, std::bitset<64> bit_flags, const rpc::Cookie &cookie, const std::string &message_payload) const
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

        const std::array<net::const_buffer, 7> payloads
                {
                        net::buffer(&payload_length, sizeof(payload_length)),
                        net::buffer(&command_id, sizeof(command_id)),
                        net::buffer(&flags, sizeof(flags)),
                        net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
                        net::buffer(&message_payload_size, sizeof(message_payload_size)),
                        net::buffer(cookie_payload),
                        net::buffer(message_payload)
                };

        payload_length = std::accumulate(
                payloads.begin(), payloads.end(), 0ULL, [](auto current, auto &buffer)
                { return current + buffer.size(); }) - sizeof(payload_length);

        std::string result_payloads(payload_length + sizeof(payload_length), '\0');
        net::buffer_copy(net::buffer(result_payloads), payloads, result_payloads.size());

        return result_payloads;
    }

    std::string stub_base::unpack(net::const_buffer payload, uint64_t &command_id, std::bitset<64> &bit_flags, rpc::Cookie &cookie) const
    {
        if (payload.size() > MAX_PAYLOAD_SIZE)
        {
            throw sys::system_error(system_error::data_corrupted);
        }

        uint64_t cookie_payload_size = 0;
        uint64_t message_payload_size = 0;
        uint64_t flags;
        const std::array header_payload
                {
                        net::buffer(&command_id, sizeof(command_id)),
                        net::buffer(&flags, sizeof(flags)),
                        net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
                        net::buffer(&message_payload_size, sizeof(message_payload_size)),
                };


        net::buffer_copy(header_payload, payload, sizeof(uint64_t) * header_payload.size());

        bit_flags = std::bitset<64>(flags);

        payload += sizeof(uint64_t) * header_payload.size();

        if (!cookie.ParseFromArray(payload.data(), static_cast<int>(cookie_payload_size)))
        {
            throw sys::system_error(system_error::data_corrupted);
        }

        payload += cookie_payload_size;

        std::string message_payload(message_payload_size, '\0');
        net::buffer_copy(net::buffer(message_payload), payload, message_payload_size);
        payload += message_payload_size;

        if (payload.size() != 0)
        {
            throw sys::system_error(system_error::data_corrupted);
        }
        return message_payload;
    }

    uint64_t stub_base::generate_trace_id()
    {
        return trace_id_max_++;
    }

    net::awaitable<void> stub_base::dispatch_request(sender_channel_t &sender_channel, uint64_t command_id, rpc::Cookie cookie, std::string request_message_payload) const
    {
        auto run_method = [this, command_id, cookie = std::move(cookie), request_message_payload = std::move(request_message_payload), &sender_channel]() mutable -> net::awaitable<void>
        {
            spdlog::debug("{} dispatch_request, cmd_id: {} cookie: \"{}\"", this->id(), command_id, cookie.Utf8DebugString());

            result<std::string> result = co_await std::invoke(method_group_, command_id, std::move(request_message_payload));

            std::string response_message_payload;
            if (result.error())
            {
                spdlog::error(
                        "{} dispatch_request implement error, cmd_id: {} code: {} what: \"{}\"",
                        this->id(),
                        command_id,
                        result.error().value(),
                        result.error().message());
                cookie.set_error_code(result.error().value());
            }
            else
            {
                response_message_payload = std::move(result.value());
            }

            const auto flags = std::bitset<64>{}.set(detail::flag_is_request, false).to_ullong();
            auto payloads = pack(command_id, flags, cookie, response_message_payload);

            co_await sender_channel.async_send({}, std::move(payloads), net::use_awaitable);
        };

        net::co_spawn(co_await net::this_coro::executor, run_method, net::detached);
    }

    net::awaitable<void> stub_base::dispatch_response(uint64_t command_id, rpc::Cookie cookie, std::string response_message_payload)
    {
        uint64_t trace_id = cookie.trace_id();

        if (!calling_.contains(trace_id))
        {
            spdlog::info("{} dispatch_response message out of date, cmd_id: {} cookie: \"{}\"", this->id(), command_id, cookie.ShortDebugString());
            co_return;
        }

        co_await calling_[trace_id]->async_send({}, std::move(cookie), std::move(response_message_payload), net::use_awaitable);
    }
}
