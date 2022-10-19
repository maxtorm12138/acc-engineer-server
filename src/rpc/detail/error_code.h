#ifndef ACC_ENGINEER_SERVER_RPC_DETAIL_ERROR_CODE_H
#define ACC_ENGINEER_SERVER_RPC_DETAIL_ERROR_CODE_H

#include <boost/system/error_code.hpp>

namespace acc_engineer::rpc::detail {
namespace sys = boost::system;

enum class system_error
{
    success = 0,
    proto_serialize_fail = 1,
    proto_parse_fail = 2,
    method_not_implement = 3,
    exception_occur = 4,
    data_corrupted = 5,
    operation_timeout = 6,
};

class system_error_category_impl : public sys::error_category
{
public:
    const char *name() const noexcept final
    {
        return "rpc system error";
    }

    std::string message(int code) const final
    {
        switch (static_cast<system_error>(code))
        {
        case system_error::success:
            return "success";
        case system_error::proto_serialize_fail:
            return "protobuf serialize fail";
        case system_error::proto_parse_fail:
            return "protobuf parse fail";
        case system_error::method_not_implement:
            return "method not implement";
        case system_error::exception_occur:
            return "exception occur";
        case system_error::data_corrupted:
            return "data corrupted";
        case system_error::operation_timeout:
            return "operation timeout";
        default:
            return "unknown";
        }
    }

    boost::system::error_condition default_error_condition(int code) const noexcept final
    {
        return {code, *this};
    }
};

extern inline const system_error_category_impl &system_error_category()
{
    static system_error_category_impl instance;
    return instance;
}

inline boost::system::error_code make_error_code(system_error error)
{
    return {static_cast<int>(error), system_error_category()};
}
} // namespace acc_engineer::rpc::detail

template<>
struct boost::system::is_error_code_enum<acc_engineer::rpc::detail::system_error> : std::true_type
{};

#endif // ACC_ENGINEER_SERVER_RPC_DETAIL_ERROR_CODE_H
