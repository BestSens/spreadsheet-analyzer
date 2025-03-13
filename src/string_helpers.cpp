#include "string_helpers.hpp"

#include <algorithm>
#include <string>
#include <string_view>

auto trim(std::string_view str) -> std::string_view {
	const auto pos1 = str.find_first_not_of(" \t\n\r");
	if (pos1 == std::string::npos) {
		return "";
	}

	const auto pos2 = str.find_last_not_of(" \t\n\r");
	return str.substr(pos1, pos2 - pos1 + 1);
}

auto stripUnit(std::string_view header) -> std::pair<std::string, std::string> {
	const auto pos = header.find_last_of('(');
	if (pos == std::string::npos) {
		return {std::string(header), ""};
	}

	const auto name = header.substr(0, pos);
	const auto unit = header.substr(pos + 1, header.find_last_of(')') - pos - 1);

	if (unit.size() > 5) {
		return {std::string(header), ""};
	}

	return {std::string(trim(name)), std::string(trim(unit))};
}

auto getIncrementedWindowTitle(const std::string &title) -> std::string {
	const auto pos = title.find_last_of('(');
	if (pos == std::string::npos) {
		return title + " (1)";
	}

	const auto base_title = title.substr(0, pos);
	const auto number_str = title.substr(pos + 1, title.find_last_of(')') - pos - 1);

	int number = 0;
	try {
		number = std::stoi(number_str);
	} catch (...) {
		return title + " (1)";
	}

	return base_title + " (" + std::to_string(number + 1) + ")";
}