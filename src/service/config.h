#ifndef ACC_ENGINEER_SERVER_SERVICE_CONFIG_H
#define ACC_ENGINEER_SERVER_SERVICE_CONFIG_H

#include <boost/asio/ip/address.hpp>
#include <boost/program_options.hpp>

namespace acc_engineer {
class config
{
public:
    [[nodiscard]] boost::asio::ip::address address() const
    {
        return bind_address_;
    }

    [[nodiscard]] uint16_t port() const
    {
        return bind_port_;
    }
    [[nodiscard]] std::string password() const
    {
        return password_;
    }

    static config from_command_line(int argc, char *argv[]);

    static config from_string(const std::string &argv);

private:
    boost::asio::ip::address bind_address_;
    uint16_t bind_port_{0};
    std::string password_;
};
} // namespace acc_engineer

#endif // ACC_ENGINEER_SERVER_CONFIG_H
