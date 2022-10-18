#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_BASE_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_BASE_H

// boost
#include <boost/asio/read.hpp>

#include "../method_group.h"
#include "error_code.h"
#include "type_requirements.h"


namespace acc_engineer::rpc::detail
{
    enum class stub_status
    {
        idle = 1,
        running = 2,
        stopping = 3,
        stopped = 4,
    };

    template<method_channel MethodChannel>
    class stub_base
    {
    public:
        explicit stub_base(MethodChannel channel);

        uint64_t id() const;

        stub_status status() const;

    protected:

        std::string pack(uint64_t command_id, std::bitset<64> bit_flags, const rpc::Cookie &cookie, const std::string &message_payload);

        std::string unpack(net::const_buffer payload, uint64_t &cmd_id, std::bitset<64> &flag, rpc::Cookie &cookie);

        uint64_t generate_trace_id();;

        MethodChannel method_channel_;
        const uint64_t stub_id_;
        sender_channel_t sender_channel_;
        stub_status status_{stub_status::idle};
        std::unordered_map<uint64_t, reply_channel_t *> calling_{};

    private:
        static std::atomic<uint64_t> stub_id_max_;
        static std::atomic<uint64_t> trace_id_max_;
    };


    template<method_channel MethodChannel>
    std::atomic<uint64_t> stub_base<MethodChannel>::stub_id_max_;

    template<method_channel MethodChannel>
    std::atomic<uint64_t> stub_base<MethodChannel>::trace_id_max_;

    template<method_channel MethodChannel>
    stub_base<MethodChannel>::stub_base(MethodChannel method_channel) :
            method_channel_(std::move(method_channel)),
            stub_id_(stub_id_max_++),
            sender_channel_(method_channel.get_executor())
    {}

    template<method_channel MethodChannel>
    uint64_t stub_base<MethodChannel>::id() const
    {
        return stub_id_;
    }

    template<method_channel MethodChannel>
    stub_status stub_base<MethodChannel>::status() const
    {
        return status_;
    }

    template<method_channel MethodChannel>
    std::string stub_base<MethodChannel>::pack(uint64_t command_id, std::bitset<64> bit_flags, const rpc::Cookie &cookie, const std::string &message_payload)
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

    template<method_channel MethodChannel>
    std::string stub_base<MethodChannel>::unpack(net::const_buffer payload, uint64_t &command_id, std::bitset<64> &bit_flags, rpc::Cookie &cookie)
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

    template<method_channel MethodChannel>
    uint64_t stub_base<MethodChannel>::generate_trace_id()
    {
        return trace_id_max_++;
    }
}

#endif //ACC_ENGINEER_SERVER_RPC_DETAIL_STUB_BASE_H
