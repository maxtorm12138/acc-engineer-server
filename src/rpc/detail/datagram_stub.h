#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_DATAGRAM_STUB_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_DATAGRAM_STUB_H

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include "stub_base.h"

namespace acc_engineer::rpc::detail {
template <async_datagram MethodChannel>
class datagram_stub : public stub_base {
 public:
  explicit datagram_stub(
      MethodChannel method_channel,
      const method_group& method_group = method_group::empty_method_group());

  net::awaitable<void> run(net::const_buffer initial = {});

  template <method_message Message>
  net::awaitable<response_t<Message>> async_call(
      const request_t<Message>& request);

 private:
  net::awaitable<void> sender_loop();

  net::awaitable<void> receiver_loop();

  MethodChannel method_channel_;
  sender_channel_t sender_channel_;
};

template <async_datagram MethodChannel>
datagram_stub<MethodChannel>::datagram_stub(MethodChannel method_channel,
                                            const method_group& method_group)
    : stub_base(method_group),
      method_channel_(std::move(method_channel)),
      sender_channel_(method_channel_.get_executor()) {}

template <async_datagram MethodChannel>
net::awaitable<void> datagram_stub<MethodChannel>::run(
    net::const_buffer initial) {
  status_ = stub_status::running;
  if (initial.size() > 0) {
    co_await dispatch(sender_channel_, initial);
  }

  net::co_spawn(co_await net::this_coro::executor, sender_loop(),
                net::detached);
  net::co_spawn(co_await net::this_coro::executor, receiver_loop(),
                net::detached);
}

template <async_datagram MethodChannel>
net::awaitable<void> datagram_stub<MethodChannel>::sender_loop() {
  using namespace std::chrono_literals;
  using namespace boost::asio::experimental::awaitable_operators;
  using detail::stub_status;

  net::steady_timer sender_timer(co_await net::this_coro::executor);
  while (status_ == stub_status::running) {
    std::string buffers_to_send =
        co_await this->sender_channel_.async_receive(net::use_awaitable);
    sender_timer.expires_after(500ms);
    co_await (method_channel_.async_send(net::buffer(buffers_to_send),
                                         net::use_awaitable) ||
              sender_timer.async_wait(net::use_awaitable));
    spdlog::debug("{} sender_loop send size {}", id(), buffers_to_send.size());
  }
}

template <async_datagram MethodChannel>
net::awaitable<void> datagram_stub<MethodChannel>::receiver_loop() {
  using namespace std::chrono_literals;
  using namespace boost::asio::experimental::awaitable_operators;
  using detail::stub_status;

  std::string payload(MAX_PAYLOAD_SIZE, '\0');
  while (this->status_ == stub_status::running) {
    size_t size_read = co_await method_channel_.async_receive(
        net::buffer(payload), net::use_awaitable);
    spdlog::debug("{} receiver_loop received size {}", id(), size_read);
    co_await dispatch(sender_channel_, net::buffer(payload, size_read));
  }
}

template <async_datagram MethodChannel>
template <method_message Message>
net::awaitable<response_t<Message>> datagram_stub<MethodChannel>::async_call(
    const request_t<Message>& request) {
  return do_async_call<Message>(sender_channel_, request);
}
}  // namespace acc_engineer::rpc::detail

#endif  // ACC_ENGINEER_SERVER_RPC_DETAIL_DATAGRAM_STUB_H
