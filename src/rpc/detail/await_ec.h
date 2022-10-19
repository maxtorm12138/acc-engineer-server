#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_AWAIT_EC_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_AWAIT_EC_H

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace acc_engineer::rpc::detail {

namespace net = boost::asio;

struct await_ec_t
{
    inline net::redirect_error_t<std::decay_t<decltype(net::use_awaitable)>> operator[](boost::system::error_code &ec) const noexcept
    {
        return boost::asio::redirect_error(net::use_awaitable, ec);
    }
};

constexpr await_ec_t await_ec;

} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_AWAIT_EC_H
