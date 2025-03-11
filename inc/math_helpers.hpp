#pragma once

#include <cmath>
#include <concepts>
#include <numeric>
#include <ranges>

template <std::floating_point T>
auto calcMax(std::span<const T> data) -> T {
	return *std::ranges::max_element(data);
}

template <std::floating_point T>
auto calcMin(std::span<const T> data) -> T {
	return *std::ranges::min_element(data);
}

template <std::floating_point T>
auto calcMean(std::span<const T> data) -> T {
	return std::accumulate(data.begin(), data.end(), T{0}) / static_cast<T>(data.size());
}

template <std::floating_point T>
auto calcStd(std::span<const T> data, T mean) -> T {
	return std::sqrt(std::accumulate(data.begin(), data.end(), T{0},
									 [mean](const auto &a, const auto &b) { return a + (b - mean) * (b - mean); }) /
					 static_cast<T>(data.size()));
}
