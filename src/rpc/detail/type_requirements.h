#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_TYPE_REQUIREMENTS_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_TYPE_REQUIREMENTS_H

// std
#include <concepts>

// protobuf
#include <google/protobuf/message.h>

// boost
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

namespace acc_engineer::rpc::detail
{
    namespace net = boost::asio;
    namespace sys = boost::system;

    template<typename Message>
    concept method_message =
    requires
    {
        std::derived_from<Message, google::protobuf::Message>;
        std::derived_from<typename Message::Response, google::protobuf::Message>;
        std::derived_from<typename Message::Request, google::protobuf::Message>;
    };

    template<typename Stream>
    concept async_stream =
    requires(Stream stream, net::mutable_buffer read_buffer, net::const_buffer write_buffer)
    {
        { std::is_nothrow_move_constructible_v<Stream> };
        { std::is_nothrow_move_assignable_v<Stream> };
        { net::async_read(stream, read_buffer, net::use_awaitable) };
        { net::async_write(stream, write_buffer, net::use_awaitable) };
        { stream.close() };
        { stream.get_executor() };
    };

    template<typename Datagram>
    concept async_datagram =
    requires(Datagram datagram, typename Datagram::endpoint_type endpoint, net::mutable_buffer read_buffer, net::const_buffer write_buffer)
    {
        { std::is_nothrow_move_constructible_v<Datagram> };
        { std::is_nothrow_move_assignable_v<Datagram> };
        { datagram.async_receive_from(read_buffer, endpoint, net::use_awaitable) };
        { datagram.async_receive(read_buffer, net::use_awaitable) };
        { datagram.async_send_to(write_buffer, endpoint, net::use_awaitable) };
        { datagram.async_send(write_buffer, net::use_awaitable) };
        { datagram.close() };
        { datagram.get_executor() };
    };

    template<typename MethodChannel>
    concept method_channel = async_stream<MethodChannel> || async_datagram<MethodChannel>;

    struct result_class
    {
    };

    template<typename Value>
    concept result_value =
    requires
    {
        { !std::derived_from<Value, result_class> };
        { !std::same_as<Value, sys::error_code> };
        { std::is_nothrow_move_constructible_v<Value> };
        { std::is_nothrow_move_assignable_v<Value> };
        { std::is_copy_assignable_v<Value> };
        { std::is_copy_constructible_v<Value> };
    };


    template<typename MethodMessage, typename MethodImplement>
    concept method_implement =
    requires(MethodImplement implement, const typename MethodMessage::Request &request)
    {
        { method_message<MethodMessage> };
        { std::invoke(implement, request) } -> std::same_as<net::awaitable<typename MethodMessage::Response>>;
    };
}

#endif //ACC_ENGINEER_SERVER_RPC_DETAIL_TYPE_REQUIREMENTS_H
