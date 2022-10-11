#include <boost/asio/io_context.hpp>
#include "proto/service.capnp.h"

namespace net = boost::asio;

int main(int argc, char *argv[])
{
    net::io_context context;
    context.run();
    return 0;
}
