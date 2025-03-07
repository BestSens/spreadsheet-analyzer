#pragma once

#include <filesystem>
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

auto getRawdata(const raw_data_entry_t& entry, [[maybe_unused]] raw_stream_t stream_id) -> std::vector<float>;
auto parseRawFiles(const std::vector<std::filesystem::path>& paths) -> RawDataHandler;