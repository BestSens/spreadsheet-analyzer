#include "raw_handling.hpp"

#include <filesystem>
#include <fstream>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "math_helpers.hpp"
#include "spdlog/spdlog.h"

namespace {
	auto memcpySwapBo(void *dest, const void *src, std::size_t count) -> void {
		char *dest_char = reinterpret_cast<char *>(dest);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		const char *src_char =
			reinterpret_cast<const char *>(src);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

		for (std::size_t i = 0; i < count; i++) {
			std::memcpy(dest_char + i, src_char + (count - i - 1), 1);
		}
	}

	[[maybe_unused]] auto memcpyBe(void *dest, const void *src, std::size_t count) -> void {
		static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
					  "mixed endian is not supported");

		if constexpr (std::endian::native == std::endian::big) {
			std::memcpy(dest, src, count);
		} else {
			memcpySwapBo(dest, src, count);
		}
	}

	[[maybe_unused]] auto memcpyLe(void *dest, const void *src, std::size_t count) -> void {
		static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
					  "mixed endian is not supported");

		if constexpr (std::endian::native == std::endian::little) {
			std::memcpy(dest, src, count);
		} else {
			memcpySwapBo(dest, src, count);
		}
	}

	constexpr auto getAmplitude(const uint32_t value) -> float {
		return (static_cast<float>(value & 0x0FFF) * (5.0f / 4096.0f) - 2.5f) * 2.0f;
	}

	constexpr auto getRuntime(uint32_t value) -> float {
		value = value >> 12u;

		if ((value & 0x2'0000) == 0) {
			return static_cast<float>(value) / 512.0f;
		} else {
			return static_cast<float>(value >> 4u) / 128.0f;
		}
	}

	template <typename T>
	auto getBeValue(const void *buffer, std::size_t offset = 0) -> T {
		T value{};
		memcpyBe(&value, reinterpret_cast<const char *>(buffer) + offset, sizeof(T));
		return value;
	}

	auto parseRawFile(RawDataHandler& handler, const std::filesystem::path& path) -> void {
		static constexpr auto header_size = 16;

		const auto file_size = std::filesystem::file_size(path);
		if (file_size == 0) {
			spdlog::warn("File {} is empty", path.string());
			return;
		}

		auto stream = std::ifstream(path, std::ios::binary);

		if (!stream) {
			spdlog::error("Failed to open file: {}", path.string());
			return;
		}

		auto buffer = std::vector<char>(header_size);
		size_t offset{0};

		while (stream.read(buffer.data(), header_size)) {
			const uint8_t type = static_cast<uint8_t>(buffer[0]);
			const uint32_t dt = getBeValue<uint32_t>(buffer.data(), 1);
			const time_t t0 = static_cast<time_t>(getBeValue<uint32_t>(buffer.data(), 5));

			const uint32_t meta_size = (getBeValue<uint32_t>(buffer.data(), 9) & 0xFFFF'FF00u) >> 8;
			const uint32_t data_size = getBeValue<uint32_t>(buffer.data(), 12);

			if (type >= static_cast<uint8_t>(raw_type_t::TYPE_SIZE)) {
				spdlog::warn("Unknown type {} in file {}", type, path.string());
				continue;
			}

			handler.addEntry({.timestamp = t0,
							  .path = path,
							  .offset = offset,
							  .type = static_cast<raw_type_t>(type),
							  .meta_size = meta_size,
							  .data_size = data_size,
							  .dt = static_cast<float>(dt) * 1e-6f});

			stream.seekg(static_cast<long long>(meta_size + data_size), std::ios::cur);
			offset = static_cast<size_t>(stream.tellg());
		}
	}

	struct stream_index {
		raw_stream_t stream_id;
		size_t offset;
	};

	auto parseRawStream(data_set_t &data_set, const raw_data_entry_t &entry,
						const std::vector<raw_stream_t> &requested_stream_ids) -> void {
		const auto stream_ids = [&entry]() -> std::vector<raw_stream_t> {
			switch (entry.type) {
			case raw_type_t::SYNC:
				return {raw_stream_t::AMPLITUDE};
			case raw_type_t::KS:
				return {raw_stream_t::KS};
			case raw_type_t::SYNC_INTEGRAL:
				return {raw_stream_t::AMPLITUDE, raw_stream_t::COE};
			case raw_type_t::SYNC_FULL_INT:
				return {raw_stream_t::AMPLITUDE, raw_stream_t::INT1, raw_stream_t::INT2, raw_stream_t::COE};
			case raw_type_t::KS_FLOAT:
				return {raw_stream_t::KS};
			default:
				return {};
			}
		}();

		if (!std::isfinite(data_set.dt)) {
			data_set.dt = entry.dt;
		} else {
			if (data_set.dt != entry.dt) {
				spdlog::error("Inconsistent dt values in raw data entry");
				return;
			}
		}

		const auto split_size = entry.data_size / stream_ids.size();
		const auto raw_offset = entry.offset + 16 + entry.meta_size;
		std::vector<stream_index> stream_indices{};

		for (const auto &requested_stream_id : requested_stream_ids) {
			// Check if requested stream exists in this raw data type
			auto temp_stream_id = requested_stream_id;

			if (temp_stream_id == raw_stream_t::RUNTIME) {
				temp_stream_id = raw_stream_t::AMPLITUDE;
			}

			auto stream_it = std::ranges::find(stream_ids, temp_stream_id);
			if (stream_it == stream_ids.end()) {
				spdlog::error("Requested stream {} not found in raw data entry of type {}",
							  static_cast<uint8_t>(requested_stream_id), static_cast<uint8_t>(entry.type));
				return;
			}

			// Get the index of the requested stream
			const auto stream_index = static_cast<size_t>(std::distance(stream_ids.begin(), stream_it));
			stream_indices.push_back({.stream_id = temp_stream_id, .offset = raw_offset + (stream_index * split_size)});
		}

		for (const auto &e : stream_indices) {
			const auto offset = e.offset;
			spdlog::debug("Stream ID: {}, Offset: {}", static_cast<uint8_t>(e.stream_id), offset);

			auto stream = std::ifstream(entry.path, std::ios::binary);
			stream.seekg(static_cast<long long>(offset), std::ios::beg);

			auto buffer = std::vector<char>(split_size);
			stream.read(buffer.data(), static_cast<long long>(buffer.size()));

			if (stream.gcount() != static_cast<long long>(buffer.size())) {
				spdlog::error("Failed to read stream data from file: {}", entry.path.string());
				return;
			}

			if (e.stream_id == raw_stream_t::AMPLITUDE) {
				data_set.data[raw_stream_t::AMPLITUDE].reserve(split_size);
				data_set.data[raw_stream_t::RUNTIME].reserve(split_size);

				for (size_t j = 0; j < split_size; j += sizeof(uint32_t)) {
					uint32_t bytes{};
					memcpyBe(&bytes, buffer.data() + j, sizeof(uint32_t));
					data_set.data[raw_stream_t::AMPLITUDE].push_back(getAmplitude(bytes));
					data_set.data[raw_stream_t::RUNTIME].push_back(getRuntime(bytes));
				}
			} else {
				data_set.data[e.stream_id].reserve(split_size);
	
				for (size_t j = 0; j < split_size; j += sizeof(float)) {
					float value{};
					memcpyBe(&value, buffer.data() + j, sizeof(float));
					data_set.data[e.stream_id].push_back(value);
				}
			}
		}
	}

	auto parseRawStream(const raw_data_entry_t &entry, const std::vector<raw_stream_t> &requested_stream_ids)
		-> data_set_t {
		data_set_t data_set{};
		data_set.dt = entry.dt;

		parseRawStream(data_set, entry, requested_stream_ids);

		return data_set;
	}

	auto downsample(const std::span<const float> data) -> downsampled_entry_t {
		downsampled_entry_t downsampled_data{};
		const auto count = data.size();

		if (count >= 3) {
			const auto mean = calcMean(data);
			const auto stdev = calcStd(data, mean);
			const auto min = calcMin(data);
			const auto max = calcMax(data);

			downsampled_data.mean = mean;
			downsampled_data.min = min;
			downsampled_data.max = max;
			downsampled_data.stddev = stdev;
		} else if (count > 0) {
			downsampled_data.mean = data.front();
			downsampled_data.min = data.front();
			downsampled_data.max = data.front();
			downsampled_data.stddev = 0;
		} else {
			downsampled_data.mean = std::numeric_limits<float>::quiet_NaN();
			downsampled_data.min = std::numeric_limits<float>::quiet_NaN();
			downsampled_data.max = std::numeric_limits<float>::quiet_NaN();
			downsampled_data.stddev = std::numeric_limits<float>::quiet_NaN();
		}

		return downsampled_data;
	}
}  // namespace

auto getRawdata(const raw_data_entry_t &entry, const std::vector<raw_stream_t> &requested_stream_ids) -> data_set_t {
	return parseRawStream(entry, requested_stream_ids);
}

auto getRawdata(const std::span<const raw_data_entry_t> &entries, const std::vector<raw_stream_t> &requested_stream_ids)
	-> data_set_t {
	data_set_t data_set{};
	data_set.t0 = entries.front().timestamp + 3600;
	
	for (const auto &entry : entries) {
		parseRawStream(data_set, entry, requested_stream_ids);
	}

	return data_set;
}

auto parseRawFiles(const std::vector<std::filesystem::path>& paths) -> RawDataHandler {
	RawDataHandler raw_data_handler;

	for (const auto& path : paths) {
		parseRawFile(raw_data_handler, path);
	}

	return raw_data_handler;
}

auto getDownsampled(const std::span<const raw_data_entry_t> &entries,
					const std::vector<raw_stream_t> &requested_stream_ids, size_t reduction_factor)
	-> std::vector<downsampled_data_t> {
	std::vector<downsampled_data_t> downsampled_data{};

	const auto data_set = getRawdata(entries, requested_stream_ids);

	if (data_set.data.empty()) {
		return downsampled_data;
	}

	const std::vector<float> empty_vector{};

	const auto& amplitude_data = [&data_set, &empty_vector]() -> const std::vector<float> & {
		try {
			return data_set.data.at(raw_stream_t::AMPLITUDE);
		} catch (const std::out_of_range&) {
			return empty_vector;
		}
	}();

	const auto& runtime_data = [&data_set, &empty_vector]() -> const std::vector<float> & {
		try {
			return data_set.data.at(raw_stream_t::RUNTIME);
		} catch (const std::out_of_range&) {
			return empty_vector;
		}
	}();

	const auto& int1_data = [&data_set, &empty_vector]() -> const std::vector<float> & {
		try {
			return data_set.data.at(raw_stream_t::INT1);
		} catch (const std::out_of_range&) {
			return empty_vector;
		}
	}();

	const auto& int2_data = [&data_set, &empty_vector]() -> const std::vector<float> & {
		try {
			return data_set.data.at(raw_stream_t::INT2);
		} catch (const std::out_of_range&) {
			return empty_vector;
		}
	}();

	const auto &coe_data = [&data_set, &empty_vector]() -> const std::vector<float> & {
		try {
			return data_set.data.at(raw_stream_t::COE);
		} catch (const std::out_of_range&) {
			return empty_vector;
		}
	}();

	const auto size =
		std::max({amplitude_data.size(), runtime_data.size(), int1_data.size(), int2_data.size(), coe_data.size()});

	auto get_span = [](size_t i, size_t reduction_factor, const std::vector<float>& data) -> std::span<const float> {
		if (data.size() < i) {
			return std::span<const float>{};
		}
		
		const auto count = std::min(reduction_factor, data.size() - i);

		if (count == 0) {
			return std::span<const float>{};
		}

		return std::span{data}.subspan(i, count);
	};

	downsampled_data.reserve(size / reduction_factor);

	for (size_t i = 0; i < size; i += reduction_factor) {
		if (i >= size) {
			break;
		}

		downsampled_data_t downsampled_entry{};
		downsampled_entry.date =
			static_cast<double>(data_set.t0) + static_cast<double>(data_set.dt) * static_cast<double>(i);
		downsampled_entry.amplitude = downsample(get_span(i, reduction_factor, amplitude_data));
		downsampled_entry.runtime = downsample(get_span(i, reduction_factor, runtime_data));
		downsampled_entry.int1 = downsample(get_span(i, reduction_factor, int1_data));
		downsampled_entry.int2 = downsample(get_span(i, reduction_factor, int2_data));
		downsampled_entry.coe = downsample(get_span(i, reduction_factor, coe_data));
		downsampled_data.push_back(downsampled_entry);
	}

	return downsampled_data;
}