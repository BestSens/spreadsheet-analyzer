#include "winapi.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <array>
#include <cwctype>
#endif

#ifdef __linux__
#include <unistd.h>
#include <sys/types.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

namespace {
#ifdef _WIN32
	auto getEnvVar(const char *name) -> std::optional<std::string> {
		char *buffer{nullptr};
		size_t size{};
		if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
			return std::nullopt;
		}
		std::string value{buffer};
		std::free(buffer);
		return value;
	}
#else
	auto getEnvVar(const char *name) -> std::optional<std::string> {
		const char *value = std::getenv(name);
		if (value == nullptr) {
			return std::nullopt;
		}
		return std::string(value);
	}
#endif
}  // namespace

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
#elif defined(__APPLE__)
#include <cstdio>
	// macOS: read the global AppleInterfaceStyle. If it equals "Dark", dark mode is enabled.
	{
		FILE *fp = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
		if (!fp) {
			// couldn't run the command; assume light theme
			return true;
		}
		char buf[64]{};
		bool isDark = false;
		if (fgets(buf, sizeof(buf), fp) != nullptr) {
			std::string s(buf);
			if (!s.empty() && s.back() == '\n') s.pop_back();
			if (s == "Dark" || s.find("Dark") != std::string::npos) {
				isDark = true;
			}
		}
		pclose(fp);
		return !isDark;
	}
#else
	return true;
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

auto getAppDataDirectory() -> std::filesystem::path {
#ifdef _WIN32
	if (const auto appdata = getEnvVar("APPDATA"); appdata.has_value()) {
		return std::filesystem::path(*appdata) / "SpreadsheetAnalyzer";
	}
	return std::filesystem::temp_directory_path() / "SpreadsheetAnalyzer";
#elif defined(__APPLE__)
	if (const auto home = getEnvVar("HOME"); home.has_value()) {
		return std::filesystem::path(*home) / "Library" / "Application Support" / "SpreadsheetAnalyzer";
	}
	return std::filesystem::temp_directory_path() / "SpreadsheetAnalyzer";
#else
	if (const auto xdg_config = getEnvVar("XDG_CONFIG_HOME"); xdg_config.has_value()) {
		return std::filesystem::path(*xdg_config) / "spreadsheet-analyzer";
	}
	if (const auto home = getEnvVar("HOME"); home.has_value()) {
		return std::filesystem::path(*home) / ".config" / "spreadsheet-analyzer";
	}
	return std::filesystem::temp_directory_path() / "spreadsheet-analyzer";
#endif
}

auto resolveShortcut(std::filesystem::path path) -> std::filesystem::path {
#ifdef _WIN32
	auto extension = path.extension().wstring();
	std::ranges::transform(extension, extension.begin(),
							[](wchar_t c) -> wchar_t { return static_cast<wchar_t>(std::towlower(c)); });

	if (extension != L".lnk") {
		return path;
	}

	const auto hr_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool should_uninit = SUCCEEDED(hr_init);

	auto resolved = path;

	IShellLinkW *shell_link{nullptr};
	auto hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
								reinterpret_cast<void **>(&shell_link));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

	if (SUCCEEDED(hr) && shell_link != nullptr) {
		IPersistFile *persist_file{nullptr};
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		hr = shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist_file));

		if (SUCCEEDED(hr) && persist_file != nullptr) {
			hr = persist_file->Load(path.c_str(), STGM_READ);

			if (SUCCEEDED(hr)) {
				std::array<wchar_t, MAX_PATH> target{};
				WIN32_FIND_DATAW find_data{};

				hr = shell_link->GetPath(target.data(), static_cast<int>(target.size()), &find_data, SLGP_RAWPATH);

				if (SUCCEEDED(hr) && target.at(0) != L'\0') {
					resolved = std::filesystem::path(target.data());
				}
			}

			persist_file->Release();
		}

		shell_link->Release();
	} else {
		spdlog::warn("Failed to resolve shortcut '{}': error_code={}", path.string(),
					 static_cast<unsigned long>(hr));  // NOLINT(google-runtime-int)
	}

	if (should_uninit) {
		CoUninitialize();
	}

	return resolved;
#else
	return path;
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
#elif defined(__APPLE__)
	auto command = "open \"" + url + "\"";
	system(command.c_str());
#else
#warning "Unknown OS, can't open webpages"
#endif
}