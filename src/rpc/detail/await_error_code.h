#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_AWAIT_ERROR_CODE_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_AWAIT_ERROR_CODE_H

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace acc_engineer::rpc::detail {

namespace net = boost::asio;

struct await_error_code_t
{
    net::redirect_error_t<net::use_awaitable_t<>> operator()(boost::system::error_code &ec) const noexcept
    {
        return redirect_error(net::use_awaitable, ec);
    }

    net::use_awaitable_t<> operator()() const noexcept
    {
        return net::use_awaitable;
    }
};

constexpr await_error_code_t await_error_code;

} // namespace acc_engineer::rpc::detail

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_AWAIT_ERROR_CODE_H
