#ifndef ACC_ENGINEER_SERVER_DETAIL_METHOD_BASE_H
#define ACC_ENGINEER_SERVER_DETAIL_METHOD_BASE_H

#include <boost/asio/awaitable.hpp>

#include "../result.h"

#include "proto/rpc.pb.h"

namespace acc_engineer::rpc::detail {
struct method_type_erasure {
  method_type_erasure() = default;
  method_type_erasure(const method_type_erasure&) = delete;
  method_type_erasure& operator=(const method_type_erasure&) = delete;

  virtual net::awaitable<std::string> operator()(
      std::string request_message_payload) = 0;

  virtual ~method_type_erasure() = default;
};
}
#endif //ACC_ENGINEER_SERVER_DETAIL_METHOD_BASE_H
