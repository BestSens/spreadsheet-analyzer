#pragma once

#include <algorithm>
#include <concepts>

template <std::signed_integral T>
constexpr auto fastCeil(T numerator, T denominator) -> T {
	const auto res = std::div(numerator, denominator);
	return res.rem ? (res.quot + 1) : res.quot;
}

template <std::unsigned_integral T>
constexpr auto fastCeil(T numerator, T denominator) -> T {
	return static_cast<T>(fastCeil(std::make_signed_t<T>(numerator), std::make_signed_t<T>(denominator)));
}

template <std::signed_integral T>
constexpr auto fastFloor(T numerator, T denominator) -> T {
	const auto res = std::div(numerator, denominator);

	return res.rem ? res.quot : std::max<T>(res.quot - 1, 0);
}

template <std::unsigned_integral T>
constexpr auto fastFloor(T numerator, T denominator) -> T {
	return static_cast<T>(fastFloor(std::make_signed_t<T>(numerator), std::make_signed_t<T>(denominator)));
}