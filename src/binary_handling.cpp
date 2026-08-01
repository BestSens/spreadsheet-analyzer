#include "binary_handling.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <ranges>
#include <span>
#include <utility>

#include "fmt/format.h"
#include "glaze/json/generic.hpp"
#include "spdlog/spdlog.h"
#include "uuid_generator.hpp"

namespace {
	constexpr size_t frame_header_size = 16;

	// A frame header's t0 is a whole unix second, so a contiguous stream can appear up to one
	// second off from where the previous frame ended. Anything beyond that is a real gap in the
	// recording rather than the resolution of t0, and the stream is resynced to the header.
	constexpr double resync_threshold_seconds = 2.0;

	// How the samples of one raw data block are encoded.
	enum class block_encoding_t : uint8_t {
		SYNC,	   // 4 byte slices, 20 bit propagation delay + 12 bit amplitude
		FLOAT32,   // IEEE-754 single precision
		KS		   // 2 byte samples
	};

	struct block_spec_t {
		block_encoding_t encoding;
		std::string_view name;
		std::string_view unit;
	};

	// The streams the sync block carries. Kept separate because one block yields two columns.
	constexpr std::string_view propagation_delay_name = "Propagation delay";
	constexpr std::string_view amplitude_name = "Amplitude";

	constexpr auto sync_block = block_spec_t{.encoding = block_encoding_t::SYNC, .name = {}, .unit = {}};

	constexpr auto blocks_type_0 = std::array{sync_block};
	constexpr auto blocks_type_1 =
		std::array{block_spec_t{.encoding = block_encoding_t::KS, .name = "Structure-borne sound", .unit = "V"}};
	constexpr auto blocks_type_2 =
		std::array{sync_block, block_spec_t{.encoding = block_encoding_t::FLOAT32, .name = "Integral 1", .unit = "V"}};
	constexpr auto blocks_type_3 =
		std::array{sync_block, block_spec_t{.encoding = block_encoding_t::FLOAT32, .name = "Integral 1", .unit = "V"},
				   block_spec_t{.encoding = block_encoding_t::FLOAT32, .name = "Integral 2", .unit = "V"},
				   block_spec_t{.encoding = block_encoding_t::FLOAT32, .name = "CoE", .unit = "ns"}};
	constexpr auto blocks_type_4 =
		std::array{block_spec_t{.encoding = block_encoding_t::FLOAT32, .name = "IEPE", .unit = "G"}};

	auto blocksForType(uint8_t type) -> std::span<const block_spec_t> {
		switch (type) {
		case 0:
			return blocks_type_0;
		case 1:
			return blocks_type_1;
		case 2:
			return blocks_type_2;
		case 3:
			return blocks_type_3;
		case 4:
			return blocks_type_4;
		default:
			return {};
		}
	}

	constexpr auto sampleSize(block_encoding_t encoding) -> size_t {
		return encoding == block_encoding_t::KS ? 2 : 4;
	}

	auto readBE(std::span<const unsigned char> buffer, size_t offset, size_t count) -> uint64_t {
		uint64_t value{0};

		for (size_t i = 0; i < count; ++i) {
			value = (value << 8U) | static_cast<uint64_t>(buffer[offset + i]);
		}

		return value;
	}

	auto readFloatBE(std::span<const unsigned char> buffer, size_t offset) -> double {
		const auto bits = static_cast<uint32_t>(readBE(buffer, offset, 4));
		return static_cast<double>(std::bit_cast<float>(bits));
	}

	// Section 11.2: the first 20 bits hold the propagation delay, the following 12 bits the amplitude.
	constexpr auto decodePropagationDelay(uint32_t word) -> double {
		return static_cast<double>(word >> 12U) / 512.0 * 100.0;   // ns
	}

	constexpr auto decodeAmplitude(uint32_t raw_amplitude) -> double {
		return (static_cast<double>(raw_amplitude) - 2048.0) / 4096.0 * 10.0;  // V
	}

	constexpr auto decodeKS(uint16_t word) -> double {
		return (static_cast<double>(word) / 8192.0) - 2.5;  // V
	}

	struct accumulator_t {
		std::string name;
		std::string unit;
		std::vector<double> timestamp{};
		std::vector<double> data{};
	};

	// Keeps accumulators in insertion order so the column list keeps the order of the spec.
	// Hands out indices rather than references: appending a stream reallocates the storage.
	class accumulator_list_t {
	public:
		auto indexOf(std::string_view name, std::string_view unit) -> size_t {
			const auto it = std::ranges::find(this->entries, name, &accumulator_t::name);

			if (it != this->entries.end()) {
				return static_cast<size_t>(std::distance(this->entries.begin(), it));
			}

			this->entries.push_back({.name = std::string{name}, .unit = std::string{unit}});
			return this->entries.size() - 1;
		}

		auto at(size_t index) -> accumulator_t& {
			return this->entries[index];
		}

		[[nodiscard]] auto all() -> std::vector<accumulator_t>& {
			return this->entries;
		}

		// Frames may overlap the samples already collected. Later frames win, so drop everything
		// from the tail that is not strictly older than the frame about to be appended.
		auto truncateFrom(double timestamp) -> void {
			for (auto& entry : this->entries) {
				const auto keep =
					static_cast<size_t>(std::ranges::lower_bound(entry.timestamp, timestamp) - entry.timestamp.begin());

				if (keep < entry.timestamp.size()) {
					entry.timestamp.resize(keep);
					entry.data.resize(keep);
				}
			}
		}

	private:
		std::vector<accumulator_t> entries{};
	};

	// A scalar taken out of the frame metadata, addressed by its path from the JSON root.
	struct scalar_spec_t {
		std::string name;
		std::string unit;
		std::vector<std::string> path;
	};

	auto findValue(const glz::generic& root, const std::vector<std::string>& path) -> const glz::generic* {
		const auto* current = &root;

		for (const auto& key : path) {
			if (!current->is_object() || !current->contains(key)) {
				return nullptr;
			}

			current = &current->at(key);
		}

		return current;
	}

	// glz::generic stores numbers as double and booleans as bool; get_if keeps us out of the
	// converting as<T>() overloads, which throw when the alternative does not match.
	auto toNumber(const glz::generic* value) -> double {
		if (value == nullptr) {
			return std::numeric_limits<double>::quiet_NaN();
		}

		if (const auto* number = value->get_if<double>(); number != nullptr) {
			return *number;
		}

		if (const auto* boolean = value->get_if<bool>(); boolean != nullptr) {
			return *boolean ? 1.0 : 0.0;
		}

		return std::numeric_limits<double>::quiet_NaN();
	}

	auto getString(const glz::generic& object, std::string_view key) -> std::string {
		if (!object.is_object() || !object.contains(key)) {
			return {};
		}

		const auto* value = object.at(key).get_if<std::string>();
		return value != nullptr ? *value : std::string{};
	}

	// The controller writes "None" where a value has no unit.
	auto normalizeUnit(std::string unit) -> std::string {
		if (unit == "None") {
			return {};
		}

		return unit;
	}

	// The frame header values that are worth plotting next to the measurement data.
	auto buildFrameHeaderSpecs(const glz::generic& root) -> std::vector<scalar_spec_t> {
		std::vector<scalar_spec_t> specs{};

		const auto add = [&specs, &root](std::vector<std::string> path, std::string_view unit) -> void {
			if (findValue(root, path) == nullptr) {
				return;
			}

			auto name = fmt::format("frame/{}", path.back());
			specs.push_back({.name = std::move(name), .unit = std::string{unit}, .path = std::move(path)});
		};

		add({"board_temp"}, "°C");
		add({"level"}, "");
		add({"offset"}, "");
		add({"vga"}, "");
		add({"sample_length"}, "");

		for (const auto& group : {"gate", "temp"}) {
			const auto* node = findValue(root, {group});

			if (node == nullptr || !node->is_object()) {
				continue;
			}

			for (const auto& [key, value] : node->get_object()) {
				if (!value.is_number() && !value.is_boolean()) {
					continue;
				}

				add({group, key}, std::string_view{group} == "temp" ? "°C" : "");
			}
		}

		return specs;
	}

	// The controller ships the list of values it logs, including display name and unit, in
	// channel_attributes.logging_config.data_sources. Each entry resolves to channel_data[source][identifier].
	auto buildDataSourceSpecs(const glz::generic& root) -> std::vector<scalar_spec_t> {
		std::vector<scalar_spec_t> specs{};

		const auto* data_sources = findValue(root, {"channel_attributes", "logging_config", "data_sources"});

		if (data_sources == nullptr || !data_sources->is_array()) {
			return specs;
		}

		for (const auto& entry : data_sources->get_array()) {
			const auto source = getString(entry, "source");
			const auto identifier = getString(entry, "identifier");

			if (source.empty() || identifier.empty()) {
				continue;
			}

			auto name = getString(entry, "name");

			if (name.empty()) {
				name = identifier;
			}

			specs.push_back({.name = std::move(name),
							 .unit = normalizeUnit(getString(entry, "unit")),
							 .path = {"channel_data", source, identifier}});
		}

		return specs;
	}

	// Fallback for frames without a logging_config: take every numeric leaf of channel_data.
	auto buildChannelDataSpecs(const glz::generic& root) -> std::vector<scalar_spec_t> {
		std::vector<scalar_spec_t> specs{};

		const auto* channel_data = findValue(root, {"channel_data"});

		if (channel_data == nullptr || !channel_data->is_object()) {
			return specs;
		}

		for (const auto& [source, values] : channel_data->get_object()) {
			if (!values.is_object()) {
				continue;
			}

			for (const auto& [identifier, value] : values.get_object()) {
				if (identifier == "date" || (!value.is_number() && !value.is_boolean())) {
					continue;
				}

				specs.push_back({.name = fmt::format("{}.{}", source, identifier),
								 .unit = {},
								 .path = {"channel_data", source, identifier}});
			}
		}

		return specs;
	}

	auto buildScalarSpecs(const glz::generic& root) -> std::vector<scalar_spec_t> {
		auto specs = buildDataSourceSpecs(root);

		if (specs.empty()) {
			specs = buildChannelDataSpecs(root);
		}

		auto header_specs = buildFrameHeaderSpecs(root);
		specs.insert(specs.end(), std::make_move_iterator(header_specs.begin()),
					 std::make_move_iterator(header_specs.end()));

		return specs;
	}

	auto decodeHexDigit(char c) -> int {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}

		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}

		if (c >= 'A' && c <= 'F') {
			return c - 'A' + 10;
		}

		return -1;
	}

	// dv_data holds one 12 bit sample per three hex characters, scaled like the sync amplitude.
	auto decodeDirectView(const std::string& hex) -> std::vector<float> {
		std::vector<float> samples{};
		samples.reserve(hex.size() / 3);

		for (size_t i = 0; i + 3 <= hex.size(); i += 3) {
			int value = 0;

			for (size_t j = 0; j < 3; ++j) {
				const auto digit = decodeHexDigit(hex[i + j]);

				if (digit < 0) {
					return {};
				}

				value = (value << 4) | digit;
			}

			samples.push_back(static_cast<float>(decodeAmplitude(static_cast<uint32_t>(value))));
		}

		return samples;
	}

	auto readWholeFile(const std::filesystem::path& path) -> std::vector<unsigned char> {
		std::ifstream file{path, std::ios::binary | std::ios::ate};

		if (!file) {
			return {};
		}

		const auto size = static_cast<std::streamoff>(file.tellg());

		if (size <= 0) {
			return {};
		}

		std::vector<unsigned char> buffer(static_cast<size_t>(size));
		file.seekg(0);
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		file.read(reinterpret_cast<char*>(buffer.data()), size);

		if (!file) {
			buffer.resize(static_cast<size_t>(file.gcount()));
		}

		return buffer;
	}

	auto decodeBlock(std::span<const unsigned char> raw, size_t block_offset, size_t block_bytes,
					 const block_spec_t& spec, std::span<const double> timestamps,
					 accumulator_list_t& accumulators) -> void {
		const auto stride = sampleSize(spec.encoding);
		const auto count = std::min(block_bytes / stride, timestamps.size());

		switch (spec.encoding) {
		case block_encoding_t::SYNC: {
			// Both indices first: the second lookup may append and reallocate.
			const auto delay_index = accumulators.indexOf(propagation_delay_name, "ns");
			const auto amplitude_index = accumulators.indexOf(amplitude_name, "V");

			auto& delay = accumulators.at(delay_index);
			auto& amplitude = accumulators.at(amplitude_index);

			for (size_t i = 0; i < count; ++i) {
				const auto word = static_cast<uint32_t>(readBE(raw, block_offset + (i * stride), 4));

				delay.timestamp.push_back(timestamps[i]);
				delay.data.push_back(decodePropagationDelay(word));

				amplitude.timestamp.push_back(timestamps[i]);
				amplitude.data.push_back(decodeAmplitude(word & 0xFFFU));
			}

			break;
		}
		case block_encoding_t::FLOAT32: {
			auto& stream = accumulators.at(accumulators.indexOf(spec.name, spec.unit));

			for (size_t i = 0; i < count; ++i) {
				stream.timestamp.push_back(timestamps[i]);
				stream.data.push_back(readFloatBE(raw, block_offset + (i * stride)));
			}

			break;
		}
		case block_encoding_t::KS: {
			auto& stream = accumulators.at(accumulators.indexOf(spec.name, spec.unit));

			for (size_t i = 0; i < count; ++i) {
				const auto word = static_cast<uint16_t>(readBE(raw, block_offset + (i * stride), 2));

				stream.timestamp.push_back(timestamps[i]);
				stream.data.push_back(decodeKS(word));
			}

			break;
		}
		}
	}

	auto toDataDict(std::string name, std::string unit, std::vector<double> timestamp,
					std::vector<double> data) -> data_dict_t {
		data_dict_t dict{};
		dict.name = std::move(name);
		dict.uuid = uuids::to_string(UUIDGenerator::getInstance().generate());
		dict.unit = std::move(unit);

		const auto is_boolean = std::ranges::all_of(
			data, [](const auto& value) -> bool { return value == 0.0 || value == 1.0 || std::isnan(value); });
		dict.data_type = is_boolean ? data_type_t::BOOLEAN : data_type_t::FLOAT;

		if (timestamp.size() > 1) {
			// median of the sample spacings, matching what the CSV loader stores
			std::vector<double> deltas{};
			deltas.reserve(timestamp.size() - 1);

			for (size_t i = 1; i < timestamp.size(); ++i) {
				deltas.push_back(timestamp[i] - timestamp[i - 1]);
			}

			const auto middle = deltas.size() / 2;
			std::ranges::nth_element(deltas, deltas.begin() + static_cast<std::ptrdiff_t>(middle));
			dict.delta_t = deltas[middle];
		}

		*dict.timestamp = std::move(timestamp);
		*dict.data = std::move(data);

		return dict;
	}

	// All raw streams of a window share one time grid; collapse them onto a single vector so a
	// day worth of files does not carry one copy of the timestamps per stream.
	auto shareTimestamps(std::vector<data_dict_t>& columns, size_t first, size_t last) -> void {
		if (last <= first + 1) {
			return;
		}

		const auto& reference = columns[first].timestamp;

		for (size_t i = first + 1; i < last; ++i) {
			if (*columns[i].timestamp != *reference) {
				return;
			}
		}

		for (size_t i = first + 1; i < last; ++i) {
			columns[i].timestamp = reference;
		}
	}
}  // namespace

auto frameTypeName(uint8_t type) -> std::string_view {
	switch (type) {
	case 0:
		return "sync";
	case 1:
		return "ks";
	case 2:
		return "sync + integral1";
	case 3:
		return "sync + integral1 + integral2 + coe";
	case 4:
		return "iepe";
	default:
		return "unknown";
	}
}

auto loadBinaryFiles(const std::vector<std::filesystem::path>& paths, size_t& finished,
					 const std::atomic<bool>& stop_loading, std::string& parse_error_out,
					 double& current_file_progress) -> binary_data_t {
	if (paths.empty()) {
		return {};
	}

	binary_data_t result{};
	result.paths = paths;

	accumulator_list_t raw_streams{};
	std::vector<scalar_spec_t> scalar_specs{};
	std::vector<std::vector<double>> scalar_values{};
	std::vector<double> frame_timestamps{};

	// Where the sample stream has got to, i.e. the time of the next sample if the recording
	// simply continues. NaN until the first frame has been read.
	auto stream_time = std::numeric_limits<double>::quiet_NaN();

	glz::generic metadata{};

	for (size_t file_index = 0; file_index < paths.size(); ++file_index) {
		if (stop_loading) {
			return {};
		}

		const auto& path = paths[file_index];
		current_file_progress = 0.0;
		spdlog::info("Loading file: {} ({}/{})...", path.filename().string(), file_index + 1, paths.size());

		const auto buffer = readWholeFile(path);

		if (buffer.empty()) {
			spdlog::error("could not read {}", path.filename().string());

			if (parse_error_out.empty()) {
				parse_error_out = fmt::format("{}: could not be read", path.filename().string());
			}

			++finished;
			continue;
		}

		size_t offset = 0;
		size_t frames_in_file = 0;

		while (offset + frame_header_size <= buffer.size()) {
			if ((frames_in_file % 16) == 0) {
				if (stop_loading) {
					return {};
				}

				current_file_progress = static_cast<double>(offset) / static_cast<double>(buffer.size());
			}

			const auto type = static_cast<uint8_t>(buffer[offset]);
			const auto dt_us = readBE(buffer, offset + 1, 4);
			const auto t0 = readBE(buffer, offset + 5, 4);
			const auto metadata_length = static_cast<size_t>(readBE(buffer, offset + 9, 3));
			const auto payload_length = static_cast<size_t>(readBE(buffer, offset + 12, 4));

			const auto frame_size = frame_header_size + metadata_length + payload_length;

			if (offset + frame_size > buffer.size()) {
				const auto message = fmt::format("{}: truncated frame at offset {}", path.filename().string(), offset);
				spdlog::warn("{}", message);

				if (parse_error_out.empty()) {
					parse_error_out = message;
				}

				break;
			}

			const auto blocks = blocksForType(type);

			if (blocks.empty()) {
				spdlog::warn("{}: skipping frame at offset {} with unknown type {}", path.filename().string(), offset,
							 type);
				offset += frame_size;
				++frames_in_file;
				continue;
			}

			if (type == 1) {
				spdlog::warn("{}: ks frames are decoded as a single stream; the three channel interleave is "
							 "not documented",
							 path.filename().string());
			}

			const auto metadata_offset = offset + frame_header_size;
			const auto payload_offset = metadata_offset + metadata_length;

			binary_frame_t frame{};
			frame.type = type;
			frame.dt = static_cast<double>(dt_us) / 1'000'000.0;
			frame.file_index = file_index;
			frame.metadata_offset = metadata_offset;
			frame.metadata_length = metadata_length;

			// The recording is one continuous stream sampled at dt, but t0 only has one second
			// resolution, so taking it at face value tears a sub-second gap or overlap into every
			// frame boundary. Frames are therefore played back to back, and t0 is only used to
			// resync when the two have drifted apart far enough that it must be a real gap.
			const auto header_t0 = static_cast<double>(t0);
			const auto continuing = std::isfinite(stream_time);
			const auto drift = continuing ? stream_time - header_t0 : 0.0;
			const auto resync = !continuing || std::abs(drift) > resync_threshold_seconds;

			if (resync && continuing) {
				spdlog::debug("{}: frame at offset {} is {:.3f} s off the running stream, resyncing to t0",
							  path.filename().string(), offset, drift);
			}

			frame.t0 = resync ? header_t0 : stream_time;

			// Raw data: the blocks listed for this type follow each other, each payload_length / n bytes.
			if (payload_length % blocks.size() != 0) {
				const auto message = fmt::format("{}: raw data length {} at offset {} is not divisible by {} blocks",
												 path.filename().string(), payload_length, offset, blocks.size());
				spdlog::warn("{}", message);

				if (parse_error_out.empty()) {
					parse_error_out = message;
				}

				break;
			}

			const auto block_bytes = payload_length / blocks.size();
			frame.sample_count = block_bytes / sampleSize(blocks.front().encoding);

			std::vector<double> timestamps{};
			timestamps.reserve(frame.sample_count);

			for (size_t i = 0; i < frame.sample_count; ++i) {
				timestamps.push_back(frame.t0 + (static_cast<double>(i) * frame.dt));
			}

			stream_time = frame.t0 + (static_cast<double>(frame.sample_count) * frame.dt);

			// Only a resync can move the stream backwards over samples already collected.
			raw_streams.truncateFrom(frame.t0);

			for (size_t block = 0; block < blocks.size(); ++block) {
				decodeBlock(buffer, payload_offset + (block * block_bytes), block_bytes, blocks[block], timestamps,
							raw_streams);
			}

			// Metadata: settings and the measurement values held by the controller at t0.
			if (metadata_length > 0) {
				const auto json = std::string_view{
					// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					reinterpret_cast<const char*>(buffer.data() + metadata_offset), metadata_length};

				metadata.reset();

				if (const auto ec = glz::read_json(metadata, json); ec) {
					spdlog::warn("{}: could not parse the metadata of the frame at offset {}",
								 path.filename().string(), offset);
				} else {
					if (scalar_specs.empty()) {
						scalar_specs = buildScalarSpecs(metadata);
						scalar_values.resize(scalar_specs.size());
					}

					while (!frame_timestamps.empty() && frame_timestamps.back() >= frame.t0) {
						frame_timestamps.pop_back();

						for (auto& values : scalar_values) {
							values.pop_back();
						}
					}

					frame_timestamps.push_back(frame.t0);

					for (size_t i = 0; i < scalar_specs.size(); ++i) {
						scalar_values[i].push_back(toNumber(findValue(metadata, scalar_specs[i].path)));
					}

					if (metadata.contains("dv_data")) {
						if (const auto* dv = metadata.at("dv_data").get_if<std::string>(); dv != nullptr) {
							frame.directview = decodeDirectView(*dv);
						}
					}
				}
			}

			while (!result.frames.empty() && result.frames.back().t0 >= frame.t0) {
				result.frames.pop_back();
			}

			result.frames.push_back(std::move(frame));

			offset += frame_size;
			++frames_in_file;
		}

		spdlog::debug("{}: read {} frames", path.filename().string(), frames_in_file);

		current_file_progress = 0.0;
		++finished;
	}

	if (stop_loading) {
		return {};
	}

	auto& streams = raw_streams.all();
	result.columns.reserve(streams.size() + scalar_specs.size());

	for (auto& stream : streams) {
		if (stream.data.empty()) {
			continue;
		}

		result.columns.push_back(
			toDataDict(std::move(stream.name), std::move(stream.unit), stream.timestamp, std::move(stream.data)));
	}

	shareTimestamps(result.columns, 0, result.columns.size());

	const auto first_scalar_column = result.columns.size();

	for (size_t i = 0; i < scalar_specs.size(); ++i) {
		if (std::ranges::all_of(scalar_values[i], [](const auto& value) -> bool { return std::isnan(value); })) {
			continue;
		}

		result.columns.push_back(toDataDict(std::move(scalar_specs[i].name), std::move(scalar_specs[i].unit),
											frame_timestamps, std::move(scalar_values[i])));
	}

	shareTimestamps(result.columns, first_scalar_column, result.columns.size());

	spdlog::debug("loaded {} frames into {} columns", result.frames.size(), result.columns.size());

	for (const auto& column : result.columns) {
		// Frames overlap, so the loader clips them against each other; everything downstream
		// binary searches the timestamps and would silently misbehave if that ever slipped.
		if (!std::ranges::is_sorted(*column.timestamp)) {
			spdlog::error("timestamps of column {} are not sorted", column.name);
		}

		const auto mean = column.data->empty()
							  ? std::numeric_limits<double>::quiet_NaN()
							  : std::accumulate(column.data->begin(), column.data->end(), 0.0) /
									static_cast<double>(column.data->size());
		spdlog::debug("  {} [{}]: {} samples, delta_t {:g} s, mean {:g}", column.name, column.unit,
					  column.data->size(), column.delta_t, mean);
	}

	return result;
}

auto readFrameMetadata(const binary_data_t& data, size_t frame_index) -> std::string {
	if (frame_index >= data.frames.size()) {
		return {};
	}

	const auto& frame = data.frames[frame_index];

	if (frame.metadata_length == 0 || frame.file_index >= data.paths.size()) {
		return {};
	}

	std::ifstream file{data.paths[frame.file_index], std::ios::binary};

	if (!file) {
		return {};
	}

	file.seekg(static_cast<std::streamoff>(frame.metadata_offset));

	std::string json(static_cast<size_t>(frame.metadata_length), '\0');
	file.read(json.data(), static_cast<std::streamoff>(frame.metadata_length));

	if (!file) {
		return {};
	}

	return json;
}
