#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_PACKET_HANDLER_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_PACKET_HANDLER_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/awaitable.hpp>

#include "await_error_code.h"
#include "error_code.h"

namespace acc_engineer::rpc::detail {
namespace net = boost::asio;
namespace sys = boost::system;

struct tcp_packet_handler
{
    static constexpr size_t MAX_PACKET_SIZE = 1500;
    using method_channel_type = net::ip::tcp::socket;
    static constexpr uint64_t type = 1;

    static net::awaitable<sys::error_code> receive_packet(method_channel_type &chan, std::vector<uint8_t> &receive_buffer) noexcept
    {
        uint64_t payload_size = 0;
        sys::error_code error_code;
        co_await net::async_read(chan, net::buffer(&payload_size, sizeof(payload_size)), await_error_code(error_code));
        if (error_code == net::error::operation_aborted)
        {
            co_return system_error::operation_canceled;
        }

        if (error_code == net::error::eof)
        {
            co_return system_error::connection_closed;
        }

        if (error_code)
        {
            co_return error_code;
        }

        receive_buffer.resize(payload_size);

        co_await net::async_read(chan, net::buffer(receive_buffer, payload_size), await_error_code(error_code));
        if (error_code == net::error::operation_aborted)
        {
            co_return system_error::operation_canceled;
        }

        if (error_code == net::error::eof)
        {
            co_return system_error::connection_closed;
        }

        if (error_code)
        {
            co_return error_code;
        }

        co_return system_error::success;
    }

    static net::awaitable<sys::error_code> send_packet(method_channel_type &chan, std::vector<uint8_t> send_buffer) noexcept
    {
        uint64_t payload_size = send_buffer.size();
        std::array buffer_sequence = {net::buffer(&payload_size, sizeof(payload_size)), net::buffer(send_buffer)};

        sys::error_code error_code;
        co_await net::async_write(chan, buffer_sequence, await_error_code(error_code));

        if (error_code == net::error::operation_aborted)
        {
            co_return system_error::operation_canceled;
        }

        if (error_code == net::error::broken_pipe || error_code == net::error::eof)
        {
            co_return system_error::connection_closed;
        }

        if (error_code)
        {
            co_return error_code;
        }

        co_return system_error::success;
    }
};

struct udp_packet_handler
{
    static constexpr size_t MAX_PACKET_SIZE = 1500;
    using method_channel_type = net::ip::udp::socket;
    static constexpr uint64_t type = 2;

    static net::awaitable<sys::error_code> receive_packet(method_channel_type &chan, std::vector<uint8_t> &receive_buffer) noexcept
    {
        sys::error_code error_code;
        receive_buffer.resize(MAX_PACKET_SIZE);
        size_t packet_size = co_await chan.async_receive(net::buffer(receive_buffer), await_error_code(error_code));
        if (error_code == net::error::operation_aborted)
        {
            co_return system_error::operation_canceled;
        }

        if (error_code == net::error::eof)
        {
            co_return system_error::connection_closed;
        }

        if (error_code)
        {
            co_return error_code;
        }

        receive_buffer.resize(packet_size);
        co_return system_error::success;
    }

    static net::awaitable<sys::error_code> send_packet(method_channel_type &chan, std::vector<uint8_t> send_buffer) noexcept
    {
        sys::error_code error_code;
        co_await chan.async_send(net::buffer(send_buffer), await_error_code(error_code));

        if (error_code == net::error::operation_aborted)
        {
            co_return system_error::operation_canceled;
        }

        if (error_code == net::error::broken_pipe || error_code == net::error::eof)
        {
            co_return system_error::connection_closed;
        }

        if (error_code)
        {
            co_return error_code;
        }

        co_return system_error::success;
    }
};

} // namespace acc_engineer::rpc::detail

#endif
