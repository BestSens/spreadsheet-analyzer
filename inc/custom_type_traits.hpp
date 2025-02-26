#pragma once

#include <cstdint>
#include <limits>
#include <utility>

#include "tl/expected.hpp"

enum class cast_error : uint8_t {
	negative_overflow,
	positive_overflow
};

template <class T, class I>
constexpr auto safeCast(const I in) -> tl::expected<T, cast_error> {
	if (std::cmp_less(in, std::numeric_limits<T>::min())) {
		return tl::unexpected(cast_error::negative_overflow);
	}

	if (std::cmp_greater(in, std::numeric_limits<T>::max())) {
		return tl::unexpected(cast_error::positive_overflow);
	}

	return static_cast<T>(in);
}

template<class T, class I>
constexpr auto coerceCast(const I in) -> T {
	const auto ret = safeCast<T>(in);

	if (ret) {
		return *ret;
	}

	if (ret.error() == cast_error::negative_overflow) {
		return std::numeric_limits<T>::min();
	} else {
		return std::numeric_limits<T>::max();
	}
}