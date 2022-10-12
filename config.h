#ifndef ACC_ENGINEER_SERVER_CONFIG_H
#define ACC_ENGINEER_SERVER_CONFIG_H

#include <boost/asio/ip/address.hpp>
#include <boost/program_options.hpp>

namespace acc_engineer
{
    class config
    {
    public:
        [[nodiscard]] boost::asio::ip::address address() const { return  bind_address_; }
        [[nodiscard]] uint16_t port() const { return bind_port_; }
        [[nodiscard]] std::string password() const { return password_; }

    public:
        static config from_command_line(int argc, char *argv[]);

    private:
        boost::asio::ip::address bind_address_;
        uint16_t bind_port_;
        std::string password_;
    };
}


#endif //ACC_ENGINEER_SERVER_CONFIG_H
