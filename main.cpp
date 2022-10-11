#include <boost/asio/io_context.hpp>

namespace net = boost::asio;

int main(int argc, char *argv[])
{
    net::io_context context;
    context.run();
    return 0;
}
