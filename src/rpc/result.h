#ifndef ACC_ENGINEER_SERVER_RPC_RESULT_H
#define ACC_ENGINEER_SERVER_RPC_RESULT_H
#include <boost/system/error_code.hpp>

namespace acc_engineer::rpc
{
	namespace sys = boost::system;

	namespace detail
	{
		class result_base
		{
		protected:
		};

		template<typename Value>
		concept result_value = requires
		{
			!std::derived_from<Value, result_base>;
		};
	}

	template<detail::result_value T>
	class result : public detail::result_base
	{
	public:
		result(const sys::error_code& error);

		result(T&& value);

		result(const T& value);

		result(const result& other);

		result(result&& other) noexcept;

		~result() = default;

		result& operator=(const result& other);

		result& operator=(result&& other) noexcept;

		[[nodiscard]] sys::error_code error() const;

		[[nodiscard]] T& value();

		[[nodiscard]] const T& value() const;

		explicit operator bool() const;

	private:
		std::optional<T> value_;
		sys::error_code error_;
	};

	template <detail::result_value T>
	result<T>::result(const sys::error_code& error) : error_(error)
	{}

	template <detail::result_value T>
	result<T>::result(T&& value) : value_(std::forward<T>(value))
	{}

	template <detail::result_value T>
	result<T>::result(const T& value) : value_(value)
	{}

	template <detail::result_value T>
	result<T>::result(const result<T>& other) : value_(other.value_), error_(other.error_)
	{}

	template <detail::result_value T>
	result<T>::result(result<T>&& other) noexcept : value_(std::move(other.value_)), error_(other.error_)
	{}

	template <detail::result_value T>
	result<T>& result<T>::operator=(const result<T>& other)
	{
		error_ = other.error_;
		value_ = other.value_;
		return *this;
	}

	template <detail::result_value T>
	result<T>& result<T>::operator=(result<T>&& other) noexcept
	{
		error_ = std::move(other.error_);
		value_ = std::move(other.value_);
		return *this;
	}

	template <detail::result_value T>
	sys::error_code result<T>::error() const
	{
		return error_;
	}

	template <detail::result_value T>
	T& result<T>::value()
	{
		return *value_;
	}

	template <detail::result_value T>
	const T& result<T>::value() const
	{
		return *value_;
	}

	template <detail::result_value T>
	result<T>::operator bool() const
	{
		if (error_)
		{
			return false;
		}

		return value_.has_value();
	}
}
#endif //!ACC_ENGINEER_SERVER_RPC_RESULT_H
