#include "raw_handling.hpp"

#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

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

	auto parseRawStream(const raw_data_entry_t &entry) -> std::vector<std::vector<float>> {
		const auto stream_ids = [&entry]() -> std::vector<raw_stream_t> {
			switch (entry.type) {
			case raw_type_t::SYNC:
				return {raw_stream_t::AMPLITUDE, raw_stream_t::RUNTIME};
			case raw_type_t::KS:
				return {raw_stream_t::KS};
			case raw_type_t::SYNC_INTEGRAL:
				return {raw_stream_t::AMPLITUDE, raw_stream_t::RUNTIME, raw_stream_t::COE};
			case raw_type_t::SYNC_FULL_INT:
				return {raw_stream_t::AMPLITUDE, raw_stream_t::RUNTIME, raw_stream_t::COE, raw_stream_t::INT1,
						raw_stream_t::INT2};
			case raw_type_t::KS_FLOAT:
				return {raw_stream_t::KS};
			default:
				return {};
			}
		}();

		const auto split_size = entry.data_size / stream_ids.size();
		const auto raw_offset = entry.offset + 16 + entry.meta_size;

		std::vector<std::vector<float>> result{};

		for (size_t i = 0; const auto &stream_id : stream_ids) {
			const auto offset = raw_offset + (i * split_size);
			spdlog::debug("Stream ID: {}, Offset: {}", static_cast<uint8_t>(stream_id), offset);

			auto stream = std::ifstream(entry.path, std::ios::binary);
			stream.seekg(static_cast<long long>(offset), std::ios::beg);

			auto buffer = std::vector<char>(split_size);
			stream.read(buffer.data(), static_cast<long long>(split_size));

			if (stream.gcount() != static_cast<long long>(split_size)) {
				spdlog::error("Failed to read stream data from file: {}", entry.path.string());
				return {};
			}

			std::vector<float> data{};
			data.reserve(split_size);

			for (size_t j = 0; j < split_size; j += sizeof(float)) {
				float value{};
				memcpyBe(&value, buffer.data() + j, sizeof(float));
				data.push_back(value);
			}

			result.push_back(data);
			++i;
		}

		return result;
	}
}  // namespace

auto getRawdata(const raw_data_entry_t& entry, [[maybe_unused]] raw_stream_t stream_id) -> std::vector<float> {
	return parseRawStream(entry).at(0);
}

auto parseRawFiles(const std::vector<std::filesystem::path>& paths) -> RawDataHandler {
	RawDataHandler raw_data_handler;

	for (const auto& path : paths) {
		parseRawFile(raw_data_handler, path);
	}

	return raw_data_handler;
}