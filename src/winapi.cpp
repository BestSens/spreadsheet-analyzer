#include "winapi.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdexcept>
#include <string>
#include <vector>

#ifdef __linux__
auto executeCmd(const std::vector<std::string> &argsVector) -> void {
	std::vector<char *> cArgsVector;

	for (const auto &str : argsVector) {
		cArgsVector.push_back(const_cast<char *>(str.c_str()));
	}

	cArgsVector.push_back(nullptr);

	if (fork() == 0) {
		execvp(cArgsVector[0], &cArgsVector[0]);
		spdlog::error("execvp() failed: {}", strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#endif

auto isLightTheme() -> bool {
#ifdef _WIN32
	// based on
	// https://stackoverflow.com/questions/51334674/how-to-detect-windows-10-light-dark-mode-in-win32-application

	// The value is expected to be a REG_DWORD, which is a signed 32-bit little-endian
	auto buffer = std::vector<char>(4);
	auto cb_data = static_cast<DWORD>(buffer.size() * sizeof(char));
	auto res = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
							L"AppsUseLightTheme",
							RRF_RT_REG_DWORD,  // expected value type
							nullptr, buffer.data(), &cb_data);

	if (res != ERROR_SUCCESS) {
		throw std::runtime_error("Error: error_code=" + std::to_string(res));
	}

	// convert bytes written to our buffer to an int, assuming little-endian
	auto i = int(buffer[3] << 24 | buffer[2] << 16 | buffer[1] << 8 | buffer[0]);

	return i == 1;
#else
	return false;
#endif
}

auto hideConsole() -> void {
#ifdef _WIN32
	auto *console = GetConsoleWindow();
	DWORD process_id{};
	GetWindowThreadProcessId(console, &process_id);
	if (GetCurrentProcessId() == process_id) {
		ShowWindow(console, SW_HIDE);
		RedrawWindow(console, nullptr, nullptr, RDW_UPDATENOW);
	}
#endif
}

auto openWebpage(std::string url) -> void {
	if (url.find("://") == std::string::npos) {
		url = "https://" + url;
	}

#if defined(_WIN32)
	ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__linux__)
	executeCmd({"xdg-open", url});
#else
#warning "Unknown OS, can't open webpages"
#endif
}