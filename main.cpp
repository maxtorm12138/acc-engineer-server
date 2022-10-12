#include <boost/asio/io_context.hpp>

#include "config.h"
#include "service.h"

namespace net = boost::asio;

int main(int argc, char *argv[])
{
    auto config = acc_engineer::config::from_command_line(argc, argv);

    net::io_context context;
    acc_engineer::service service(context, config);

    service.run();
    context.run();
    return 0;
}
