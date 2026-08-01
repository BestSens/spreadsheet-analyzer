#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "dicts.hpp"

// One frame of a BeMoS one raw data file. See "Handbuch BeMoS one", section 11.2 "Rohdatenformat":
// a file is a sequence of frames, each made up of a 16 byte header, a JSON metadata field and a
// raw data field. All integers are stored in network byte order (big endian).
struct binary_frame_t {
	double t0{};				// unix timestamp of the first sample, seconds
	double dt{};				// spacing between two samples, seconds
	uint8_t type{};				// frame type, selects which streams the raw data field holds
	size_t sample_count{};		// samples per stream in this frame

	// DirectView snapshot in volts, decoded from the metadata's dv_data. Empty when absent.
	std::vector<float> directview{};

	// Where the metadata JSON lives, so it can be re-read on demand instead of being kept around.
	size_t file_index{};
	uint64_t metadata_offset{};
	uint64_t metadata_length{};
};

struct binary_data_t {
	std::vector<data_dict_t> columns{};
	std::vector<binary_frame_t> frames{};	 // ordered by t0
	std::vector<std::filesystem::path> paths{};
};

[[nodiscard]] auto loadBinaryFiles(const std::vector<std::filesystem::path>& paths, size_t& finished,
								   const std::atomic<bool>& stop_loading, std::string& parse_error_out,
								   double& current_file_progress) -> binary_data_t;

// Reads the metadata JSON of a single frame back from disk. Returns an empty string on failure.
[[nodiscard]] auto readFrameMetadata(const binary_data_t& data, size_t frame_index) -> std::string;

[[nodiscard]] auto frameTypeName(uint8_t type) -> std::string_view;
