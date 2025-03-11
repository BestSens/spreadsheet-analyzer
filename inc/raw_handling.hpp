#pragma once

#include <filesystem>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

enum class raw_type_t : uint8_t {
	SYNC = 0,
	KS = 1,
	SYNC_INTEGRAL = 2,
	SYNC_FULL_INT = 3,
	KS_FLOAT = 4,
	TYPE_SIZE  // NOLINT(cert-int09-c)
};

enum class raw_stream_t : uint8_t {
	AMPLITUDE,
	RUNTIME,
	COE,
	INT1,
	INT2,
	KS
};

struct raw_data_entry_t {
	time_t timestamp;
	std::filesystem::path path;
	size_t offset;
	raw_type_t type;
	size_t meta_size;
	size_t data_size;
	float dt;
};

struct downsampled_entry_t {
	float mean{};
	float min{};
	float max{};
	float stddev{};
};

struct downsampled_data_t {
	double date{};
	downsampled_entry_t amplitude{};
	downsampled_entry_t runtime{};
	downsampled_entry_t coe{};
	downsampled_entry_t int1{};
	downsampled_entry_t int2{};
};

struct data_set_t {
	time_t t0{};
	float dt{std::numeric_limits<float>::quiet_NaN()};
	std::unordered_map<raw_stream_t, std::vector<float>> data;
};

class RawDataHandler {
public:
	RawDataHandler() = default;
	explicit RawDataHandler(std::vector<raw_data_entry_t> new_entries) : entries{std::move(new_entries)} {}

	auto addEntry(const raw_data_entry_t &entry) -> void {
		this->entries.push_back(entry);
	}

	[[nodiscard]] auto getEntries() const -> const std::vector<raw_data_entry_t> & {
		return this->entries;
	}

private:
	std::vector<raw_data_entry_t> entries{};
};

auto getRawdata(const raw_data_entry_t& entry, const std::vector<raw_stream_t> &requested_stream_ids) -> data_set_t;
auto getRawdata(const std::span<const raw_data_entry_t> &entries, const std::vector<raw_stream_t> &requested_stream_ids)
	-> data_set_t;
auto getDownsampled(const std::span<const raw_data_entry_t> &entries,
					const std::vector<raw_stream_t> &requested_stream_ids, size_t reduction_factor)
	-> std::vector<downsampled_data_t>;
auto parseRawFiles(const std::vector<std::filesystem::path>& paths) -> RawDataHandler;