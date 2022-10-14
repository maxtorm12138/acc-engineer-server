#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <iostream>

using namespace boost::asio;


using my_channel_t = use_awaitable_t<>::as_default_on_t<experimental::channel<void(boost::system::error_code, std::string)>>;

awaitable<void> echo(my_channel_t &chan)
{
	for (;;)
	{
		const std::string message = co_await chan.async_receive(use_awaitable);
		co_await chan.async_send({}, "MessageReply: " + message);
	}
}

awaitable<void> sender(my_channel_t &chan)
{
	for (;;)
	{
		std::string message;
		std::getline(std::cin, message);

		co_await chan.async_send({}, message);

		message = co_await chan.async_receive(use_awaitable);

		std::cout << message << std::endl;
	}
}

int main(int argc, char *argv[])
{
	io_context ctx;
	my_channel_t chan(ctx, 20);
	co_spawn(ctx, echo(chan), detached);
	co_spawn(ctx, sender(chan), detached);

	ctx.run();
}