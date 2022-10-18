#ifndef ACC_ENGINEER_SERVER_RPC_STUB_H
#define ACC_ENGINEER_SERVER_RPC_STUB_H

// std
#include <bitset>
#include <numeric>

// log
#include <spdlog/spdlog.h>

// boost
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/noncopyable.hpp>

// module
#include "detail/type_requirements.h"
#include "detail/stub_base.h"
#include "detail/error_code.h"
#include "detail/types.h"
#include "method_group.h"

namespace acc_engineer::rpc
{
    namespace sys = boost::system;
    namespace net = boost::asio;

    template<detail::method_channel MethodChannel>
    class stub final : public detail::stub_base<MethodChannel>
    {
    public:
        explicit stub(MethodChannel channel, const method_group &method_group = method_group::empty_method_group()) :
                detail::stub_base<MethodChannel>(std::move(channel)),
                method_group_(method_group)
        {}

        net::awaitable<void> run(net::const_buffer initial = {})
        {
            this->status_ = detail::stub_status::running;
            if (initial.size() > 0)
            {
                co_await handle_initial(initial);
            }

            net::co_spawn(co_await net::this_coro::executor, sender_loop(), net::detached);
            net::co_spawn(co_await net::this_coro::executor, receiver_loop(), net::detached);
        }


        template<detail::method_message Message>
        net::awaitable<detail::response_t<Message>> async_call(const detail::request_t<Message> &request);

    private:
        net::awaitable<void> sender_loop();

        template<detail::async_stream Stream>
        net::awaitable<void> do_sender_loop(Stream &stream);

        template<detail::async_datagram Datagram>
        net::awaitable<void> do_sender_loop(Datagram &datagram);

        net::awaitable<void> receiver_loop();

        net::awaitable<void> handle_initial(net::const_buffer initial);

        template<detail::async_stream Stream>
        net::awaitable<void> do_receiver_loop(Stream &stream);

        template<detail::async_datagram Datagram>
        net::awaitable<void> do_receiver_loop(Datagram &datagram);

        net::awaitable<void> dispatch_request(uint64_t command_id, rpc::Cookie cookie, std::string request_message_payload);

        net::awaitable<void> dispatch_response(uint64_t command_id, rpc::Cookie cookie, std::string response_message_payload);

        const method_group &method_group_;
    };

    template<detail::method_channel MethodChannel>
    net::awaitable<void> stub<MethodChannel>::sender_loop()
    {
        co_await do_sender_loop(this->method_channel_);
    }

    template<detail::method_channel MethodChannel>
    template<detail::async_stream Stream>
    net::awaitable<void> stub<MethodChannel>::do_sender_loop(Stream &stream)
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        net::steady_timer sender_timer(co_await net::this_coro::executor);
        while (this->status_ == stub_status::running)
        {
            const std::string *buffers_to_send = co_await this->sender_channel_.async_receive(net::use_awaitable);

            sender_timer.expires_after(500ms);
            co_await (net::async_write(stream, net::buffer(*buffers_to_send), net::use_awaitable) || sender_timer.async_wait(net::use_awaitable));
        }
    }

    template<detail::method_channel MethodChannel>
    template<detail::async_datagram Datagram>
    net::awaitable<void> stub<MethodChannel>::do_sender_loop(Datagram &datagram)
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;

        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        net::steady_timer sender_timer(co_await net::this_coro::executor);
        while (this->status_ == stub_status::running)
        {
            const std::vector<net::const_buffer> *buffers_to_send = co_await this->sender_channel_.async_receive(net::use_awaitable);

            sender_timer.expires_after(500ms);
            co_await (datagram.async_send(*buffers_to_send, net::use_awaitable) || sender_timer.async_wait(net::use_awaitable));
        }
    }

    template<detail::method_channel MethodChannel>
    net::awaitable<void> stub<MethodChannel>::receiver_loop()
    {
        co_await do_receiver_loop(this->method_channel_);
    }

    template<detail::method_channel MethodChannel>
    net::awaitable<void> stub<MethodChannel>::handle_initial(net::const_buffer initial)
    {
        uint64_t payload_size = 0;
        std::vector<uint8_t> payload(1500);

        std::array segment_payload = {
                net::buffer(net::buffer(&payload_size, sizeof(payload_size))),
                net::buffer(payload)
        };

        net::buffer_copy(segment_payload, initial);

        uint64_t command_id;
        std::bitset<64> flags;
        rpc::Cookie cookie;
        auto message_payload = this->unpack(net::buffer(payload, payload_size), command_id, flags, cookie);

        if (std::bitset<64>(flags).test(detail::flag_is_request))
        {
            co_await dispatch_request(command_id, std::move(cookie), std::move(message_payload));
        }
    }

    template<detail::method_channel MethodChannel>
    template<detail::async_stream Stream>
    net::awaitable<void> stub<MethodChannel>::do_receiver_loop(Stream &stream)
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        uint64_t payload_size = 0;
        std::vector<uint8_t> payload(1500);

        while (this->status_ == stub_status::running)
        {
            co_await net::async_read(stream, net::buffer(&payload_size, sizeof(payload_size)), net::use_awaitable);
            size_t size_read = co_await net::async_read(stream, net::buffer(payload, payload_size), net::use_awaitable);
            // TODO size_read == payload_size

            uint64_t command_id;
            std::bitset<64> flags;
            rpc::Cookie cookie;
            auto message_payload = this->unpack(net::buffer(payload, payload_size), command_id, flags, cookie);

            if (std::bitset<64>(flags).test(detail::flag_is_request))
            {
                co_await dispatch_request(command_id, std::move(cookie), std::move(message_payload));
            }
            else
            {
                co_await dispatch_response(command_id, std::move(cookie), message_payload);
            }
        }
    }

    template<detail::method_channel MethodChannel>
    template<detail::async_datagram Datagram>
    net::awaitable<void> stub<MethodChannel>::do_receiver_loop(Datagram &datagram)
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        using detail::stub_status;

        uint64_t payload_size = 0;
        std::vector<uint8_t> payload(1500);

        while (this->status_ == stub_status::running)
        {
            std::array segment_payload = {
                    net::buffer(net::buffer(&payload_size, sizeof(payload_size))),
                    net::buffer(payload)
            };

            size_t size_read = co_await datagram.async_receive(segment_payload, net::use_awaitable);
            // TODO size_read == payload_size
            uint64_t command_id;
            std::bitset<64> flags;
            rpc::Cookie cookie;
            auto message_payload = this->unpack(net::buffer(payload, payload_size), command_id, flags, cookie);

            if (std::bitset<64>(flags).test(detail::flag_is_request))
            {
                co_await dispatch_request(command_id, std::move(cookie), std::move(message_payload));
            }
            else
            {
                co_await dispatch_response(command_id, std::move(cookie), message_payload);
            }
        }
    }

    template<detail::method_channel MethodChannel>
    net::awaitable<void> stub<MethodChannel>::dispatch_request(uint64_t command_id, rpc::Cookie cookie, std::string request_message_payload)
    {
        auto run_method = [this, command_id, cookie = std::move(cookie), request_message_payload = std::move(request_message_payload)]() mutable -> net::awaitable<void>
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

            auto flags = std::bitset<64>{}.set(detail::flag_is_request, false).to_ullong();
            auto payloads = this->pack(command_id, flags, cookie, response_message_payload);

            co_await this->sender_channel_.async_send({}, &payloads, net::use_awaitable);
        };

        net::co_spawn(co_await net::this_coro::executor, run_method, net::detached);

    }

    template<detail::method_channel MethodChannel>
    net::awaitable<void> stub<MethodChannel>::dispatch_response(uint64_t command_id, rpc::Cookie cookie, std::string response_message_payload)
    {
        uint64_t trace_id = cookie.trace_id();

        if (!this->calling_.contains(trace_id))
        {
            spdlog::info("{} dispatch_response message out of date, cmd_id: {} cookie: \"{}\"", this->id(), command_id, cookie.ShortDebugString());
            co_return;
        }
        co_await this->calling_[trace_id]->async_send({}, std::move(cookie), std::move(response_message_payload), net::use_awaitable);
    }


    template<detail::method_channel MethodChannel>
    template<detail::method_message Message>
    net::awaitable<detail::response_t<Message>> stub<MethodChannel>::async_call(const detail::request_t<Message> &request)
    {
        uint64_t command_id = Message::descriptor()->options().GetExtension(rpc::cmd_id);
        auto flags = std::bitset<64>{}.set(detail::flag_is_request, true);

        rpc::Cookie request_cookie;
        request_cookie.set_trace_id(this->generate_trace_id());
        request_cookie.set_error_code(0);

        std::string request_message_payload;
        if (!request.SerializeToString(&request_message_payload))
        {
            throw sys::system_error(detail::system_error::proto_serialize_fail);
        }

        auto request_payload = this->pack(command_id, flags, request_cookie, request_message_payload);

        detail::reply_channel_t reply_channel(co_await net::this_coro::executor, 1);
        this->calling_[request_cookie.trace_id()] = &reply_channel;

        co_await this->sender_channel_.async_send({}, &request_payload, net::use_awaitable);

        auto[response_cookie, response_message_payload] = co_await reply_channel.async_receive(net::use_awaitable);

        this->calling_.erase(request_cookie.trace_id());
        if (response_cookie.error_code() != 0)
        {
            throw sys::system_error(static_cast<detail::system_error>(response_cookie.error_code()));
        }

        detail::response_t<Message> response{};
        if (!response.ParseFromString(response_message_payload))
        {
            throw sys::system_error(detail::system_error::proto_parse_fail);
        }

        co_return std::move(response);

    }
}// namespace acc_engineer::rpc

#endif//ACC_ENGINEER_SERVER_RPC_STUB_H
