#ifndef ACC_ENGINEER_SERVER_RPC_STUB_H
#define ACC_ENGINEER_SERVER_RPC_STUB_H

// std
#include <bitset>

// log
#include <spdlog/spdlog.h>

// boost
#include <boost/noncopyable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

// module
#include "error_code.h"
#include "methods.h"
#include "types.h"
#include "await_ec.h"

namespace acc_engineer::rpc
{

    template<typename AsyncStream>
    class stub : public boost::noncopyable
    {
    public:
        explicit stub(AsyncStream stream, const method_group &method_group = method_group::empty_method_group());

        stub(stub &&) noexcept = delete;

        stub &operator=(stub &&) noexcept = delete;

        template<method_message Message>
        net::awaitable<result<response_t<Message>>> async_call(const request_t<Message> &request);

        uint64_t id() const;

    private:
        enum class stub_status
        {
            idle = 1,
            running = 2,
            stopping = 3,
            stopped = 4,
        };

        using sender_channel_type = net::experimental::channel<void(sys::error_code, const std::vector<net::const_buffer> *)>;

        using reply_channel_type = net::experimental::channel<void(sys::error_code, rpc::Cookie, payload_t)>;

        uint64_t generate_trace_id();

        static uint64_t generate_stub_id();

        net::awaitable<void> sender_loop();

        net::awaitable<void> receiver_loop();

        void stub_error();

        net::awaitable<void> dispatch_request(uint64_t command_id, payload_t request_cookie_payload, payload_t request_message_payload);

        net::awaitable<void> dispatch_response(uint64_t command_id, payload_t response_cookie_payload, payload_t response_message_payload);

        AsyncStream stream_;
        const method_group &method_group_;
        sender_channel_type sender_channel_;
        const uint64_t stub_id_;
        stub_status status_{stub_status::idle};
        uint64_t trace_id_current_{0};
        std::unordered_map<uint64_t, reply_channel_type *> calling_{};
    };

    template<typename AsyncStream>
    stub<AsyncStream>::stub(AsyncStream stream, const method_group &method_group) :
            stream_(std::move(stream)),
            method_group_(method_group),
            sender_channel_(stream_.get_executor()),
            stub_id_(generate_stub_id())
    {
        status_ = stub_status::running;

        net::co_spawn(stream_.get_executor(), sender_loop(), net::detached);
        net::co_spawn(stream_.get_executor(), receiver_loop(), net::detached);
    }

    template<typename AsyncStream>
    template<method_message Message>
    net::awaitable<result<response_t<Message>>> stub<AsyncStream>::async_call(const request_t<Message> &request)
    {
        try
        {
            uint64_t command_id = Message::descriptor()->options().GetExtension(rpc::cmd_id);
            uint64_t flags = std::bitset<64>{}.set(flag_is_request).to_ullong();

            rpc::Cookie request_cookie;
            request_cookie.set_trace_id(generate_trace_id());
            request_cookie.set_error_code(0);

            payload_t request_cookie_payload;
            if (!request_cookie.SerializeToString(&request_cookie_payload))
            {
                co_return system_error::proto_serialize_fail;
            }

            uint64_t request_cookie_payload_size = request_cookie_payload.size();

            payload_t request_message_payload;
            if (!request.SerializeToString(&request_message_payload))
            {
                co_return system_error::proto_serialize_fail;
            }
            uint64_t request_message_payload_size = request_message_payload.size();

            reply_channel_type reply_channel(co_await net::this_coro::executor, 1);

            calling_[request_cookie.trace_id()] = &reply_channel;

            const std::vector<net::const_buffer> request_payloads
                    {
                            net::buffer(&command_id, sizeof(command_id)),
                            net::buffer(&flags, sizeof(flags)),
                            net::buffer(&request_cookie_payload_size, sizeof(request_cookie_payload_size)),
                            net::buffer(&request_message_payload_size, sizeof(request_message_payload_size)),
                            net::buffer(request_cookie_payload),
                            net::buffer(request_message_payload)
                    };

            co_await sender_channel_.async_send({}, &request_payloads, net::use_awaitable);

            auto[response_cookie, response_message_payload] = co_await reply_channel.async_receive(net::use_awaitable);

            calling_.erase(request_cookie.trace_id());

            if (response_cookie.error_code() != 0)
            {
                co_return static_cast<system_error>(response_cookie.error_code());
            }

            response_t<Message> response{};
            if (!response.ParseFromString(response_message_payload))
            {
                co_return system_error::proto_parse_fail;
            }

            co_return std::move(response);
        }
        catch (const sys::system_error &error)
        {
            spdlog::error(R"({} async_call system_error, code "{}" what "{}")", stub_id_, error.code().value(), error.what());
            co_return error.code();
        }
        catch (const std::exception &error)
        {
            spdlog::error("{} async_call exception, what \"{}\"", stub_id_, error.what());
            co_return system_error::exception_occur;
        }
        catch (...)
        {
            spdlog::error("{} async_call unknown exception");
            co_return system_error::exception_occur;
        }
    }


    template<typename AsyncStream>
    uint64_t stub<AsyncStream>::generate_trace_id()
    {
        return trace_id_current_++;
    }

    template<typename AsyncStream>
    uint64_t stub<AsyncStream>::generate_stub_id()
    {
        static std::atomic<uint64_t> stub_id_current{0};
        return stub_id_current++;
    }

    template<typename AsyncStream>
    net::awaitable<void> stub<AsyncStream>::sender_loop()
    {
        using namespace std::chrono_literals;
        using namespace boost::asio::experimental::awaitable_operators;
        try
        {
            net::steady_timer sender_timer(co_await net::this_coro::executor);
            while (status_ == stub_status::running)
            {
                const std::vector<net::const_buffer> *buffers_to_send = co_await sender_channel_.async_receive(net::use_awaitable);

                sender_timer.expires_after(500ms);
                co_await (net::async_write(stream_, *buffers_to_send, net::use_awaitable) || sender_timer.async_wait(net::use_awaitable));
            }
        }
        catch (const sys::system_error &error)
        {
            spdlog::error(R"({} sender_loop system_error, code "{}" what "{}")", stub_id_, error.code().value(), error.what());
        }
        catch (const std::exception &error)
        {
            spdlog::error("{}, sender_loop exception, what \"{}\"", stub_id_, error.what());
        }
        catch (...)
        {
            spdlog::error("{} sender_loop unknown exception ", stub_id_);
        }

        stream_.close();
        sender_channel_.close();
    }

    template<typename AsyncStream>
    net::awaitable<void> stub<AsyncStream>::receiver_loop()
    {
        try
        {
            while (status_ == stub_status::running)
            {
                uint64_t command_id = 0;
                uint64_t flags = 0;
                uint64_t cookie_payload_size = 0;
                uint64_t message_payload_size = 0;

                std::array header_payload
                        {
                                net::buffer(&command_id, sizeof(command_id)),
                                net::buffer(&flags, sizeof(flags)),
                                net::buffer(&cookie_payload_size, sizeof(cookie_payload_size)),
                                net::buffer(&message_payload_size, sizeof(message_payload_size)),
                        };


                co_await net::async_read(stream_, header_payload, net::use_awaitable);
                payload_t cookie_payload(cookie_payload_size, '\0');
                payload_t message_payload(message_payload_size, '\0');

                std::array variable_data_payload
                        {
                                net::buffer(cookie_payload),
                                net::buffer(message_payload)
                        };

                co_await net::async_read(stream_, variable_data_payload, net::use_awaitable);

                if (std::bitset<64>(flags).test(flag_is_request))
                {
                    co_await dispatch_request(command_id, std::move(cookie_payload), std::move(message_payload));
                }
                else
                {
                    co_await dispatch_response(command_id, std::move(cookie_payload), message_payload);
                }
            }
        }
        catch (const sys::system_error &error)
        {
            spdlog::error(R"({} receiver_loop system_error, code "{}" what "{}")", stub_id_, error.code().value(), error.what());
        }
        catch (const std::exception &error)
        {
            spdlog::error("{} receiver_loop exception \"{}\"", stub_id_, error.what());
        }
        catch (...)
        {
            spdlog::error("{} receiver_loop unknown exception ", stub_id_);
        }

        stream_.close();
        sender_channel_.close();
    }

    template<typename AsyncStream>
    net::awaitable<void> stub<AsyncStream>::dispatch_request(uint64_t command_id, payload_t request_cookie_payload, payload_t request_message_payload)
    {
        auto run_method = [this, command_id, request_cookie_payload = std::move(request_cookie_payload), request_message_payload = std::move(
                request_message_payload)]() mutable -> net::awaitable<void>
        {
            rpc::Cookie cookie{};
            if (!cookie.ParseFromString(request_cookie_payload))
            {
                // TODO fatal error
                spdlog::error("{} dispatch_request parse request cookie error, cmd_id: {}", stub_id_, command_id);
                co_return;
            }

            spdlog::debug("{} dispatch_request, cmd_id: {} cookie: \"{}\"", stub_id_, command_id, cookie.Utf8DebugString());
            result<payload_t> result = co_await std::invoke(method_group_, command_id, std::move(request_message_payload));

            uint64_t response_message_payload_size = 0;
            if (result.error())
            {
                spdlog::error(
                        "{} dispatch_request implement error, cmd_id: {} code: {} what: \"{}\"",
                        stub_id_,
                        command_id,
                        result.error().value(),
                        result.error().message());
                cookie.set_error_code(result.error().value());
            }
            else
            {
                response_message_payload_size = result.value().size();
            }

            uint64_t flags = std::bitset<64>{}.set(flag_is_request, false).to_ullong();

            payload_t response_cookie_payload;
            if (!cookie.SerializeToString(&response_cookie_payload))
            {
                // TODO fatal error
                spdlog::error("{} dispatch_request serialize response cookie error, cmd_id: {}", stub_id_, command_id);
                co_return;
            }
            uint64_t response_cookie_payload_size = response_cookie_payload.size();


            const std::vector<net::const_buffer> response_payloads
                    {
                            net::buffer(&command_id, sizeof(command_id)),
                            net::buffer(&flags, sizeof(flags)),
                            net::buffer(&response_cookie_payload_size, sizeof(response_cookie_payload_size)),
                            net::buffer(&response_message_payload_size, sizeof(response_message_payload_size)),
                            net::buffer(response_cookie_payload),
                            net::buffer(result.value())
                    };

            co_await sender_channel_.async_send({}, &response_payloads, net::use_awaitable);

        };

        net::co_spawn(co_await net::this_coro::executor, run_method, net::detached);
    }

    template<typename AsyncStream>
    net::awaitable<void> stub<AsyncStream>::dispatch_response(uint64_t command_id, payload_t response_cookie_payload, payload_t response_message_payload)
    {
        rpc::Cookie cookie{};
        if (!cookie.ParseFromString(response_cookie_payload))
        {
            // TODO fatal error
            spdlog::error("{} dispatch_response parse response cookie error, cmd_id: {}", stub_id_, command_id);
            co_return;
        }

        uint64_t trace_id = cookie.trace_id();

        if (!calling_.contains(trace_id))
        {
            // TODO message outdated;
            spdlog::info("{} dispatch_response message out of date, cmd_id: {} cookie: \"{}\"", stub_id_, command_id, cookie.Utf8DebugString());
            co_return;
        }
        co_await calling_[trace_id]->async_send({}, std::move(cookie), std::move(response_message_payload), net::use_awaitable);
    }

    template<typename AsyncStream>
    uint64_t stub<AsyncStream>::id() const
    {
        return stub_id_;
    }
}

#endif //ACC_ENGINEER_SERVER_RPC_STUB_H
