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
	header = trim(header);
	if (header.empty()) {
		return {"", ""};
	}

	const char close = header.back();
	const char open = (close == ')') ? '(' : (close == ']') ? '[' : '\0';
	if (open == '\0') {
		return {std::string(header), ""};
	}

	size_t open_pos = std::string::npos;
	int depth = 0;
	for (size_t i = header.size(); i-- > 0;) {
		if (header.at(i) == close) {
			++depth;
		} else if (header.at(i) == open) {
			--depth;
			if (depth == 0) {
				open_pos = i;
				break;
			}
		}
	}

	if (open_pos == std::string::npos || open_pos + 1 >= header.size()) {
		return {std::string(header), ""};
	}

	const auto name = trim(header.substr(0, open_pos));
	const auto unit = trim(header.substr(open_pos + 1, header.size() - open_pos - 2));
	if (unit.empty() || unit.size() > 32) {
		return {std::string(header), ""};
	}

	return {std::string(name), std::string(unit)};
}

auto getIncrementedWindowTitle(const std::string &title) -> std::string {
	const auto pos = title.find_last_of('(');
	if (pos == std::string::npos) {
		return title + " (1)";
	}

	const auto close_pos = title.find(')', pos);
	if (close_pos == std::string::npos) {
		return title + " (1)";
	}

	const auto base_title = title.substr(0, pos - 1);
	const auto number_str = title.substr(pos + 1, close_pos - pos - 1);

	int number = 0;
	try {
		number = std::stoi(number_str);
	} catch (...) {
		return title + " (1)";
	}

	return base_title + " (" + std::to_string(number + 1) + ")";
}