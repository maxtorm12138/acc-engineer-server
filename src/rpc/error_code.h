#ifndef ACC_ENGINEER_SERVER_RPC_ERROR_CODE_H
#define ACC_ENGINEER_SERVER_RPC_ERROR_CODE_H

#include <boost/system/error_code.hpp>

namespace acc_engineer::rpc
{
    enum class logic_error
    {
        success = 0,
    };

    enum class system_error
    {
        success = 0,
        proto_serialize_fail = 1,
        proto_parse_fail = 2,
        method_not_implement = 3,
        exception_occur = 4,

    };

    class logic_error_category_impl : public boost::system::error_category
    {
    public:
        const char *name() const noexcept final
        { return "logic error"; }

        std::string message(int code) const final
        {
            switch (static_cast<logic_error>(code))
            {
                case logic_error::success:
                    return "success";
                default:
                    return "";
            }
        }

        boost::system::error_condition default_error_condition(int code) const noexcept final
        {
            return {code, *this};
        }
    };

    class system_error_category_impl : public boost::system::error_category
    {
    public:
        const char *name() const noexcept final
        { return "logic error"; }

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
                default:
                    return "unknown";
            }
        }

        boost::system::error_condition default_error_condition(int code) const noexcept final
        {
            return {code, *this};
        }
    };

    extern inline const logic_error_category_impl &logic_error_category()
    {
        static logic_error_category_impl instance;
        return instance;
    }

    extern inline const system_error_category_impl &system_error_category()
    {
        static system_error_category_impl instance;
        return instance;
    }

    inline boost::system::error_code make_error_code(logic_error error)
    {
        return {static_cast<int>(error), logic_error_category()};
    }

    inline boost::system::error_code make_error_code(system_error error)
    {
        return {static_cast<int>(error), logic_error_category()};
    }

}

template<>
struct boost::system::is_error_code_enum<acc_engineer::rpc::logic_error> : std::true_type
{
};

template<>
struct boost::system::is_error_code_enum<acc_engineer::rpc::system_error> : std::true_type
{
};


#endif
