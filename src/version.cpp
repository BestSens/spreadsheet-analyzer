#include "version.hpp"

#include <cctype>
#include <string>

#include "fmt/format.h"
#include "version_info.hpp"

constexpr auto stringsEqual(const char* a, const char* b) -> bool {
	return *a == *b && (*a == '\0' || stringsEqual(a + 1, b + 1));
}

auto appIsDev() -> bool {
	return !stringsEqual(app_version_branch, "master") && (std::isdigit(app_version_branch[0]) == 0);
}

auto appVersion() -> std::string {
	static const std::string version = fmt::format("{}.{}.{}", app_version_major, app_version_minor, app_version_patch);

	if (appIsDev()) {
		if (appIsDebug())
			return version + "-" + std::string(app_version_branch) + std::string(app_version_gitrev) + "-dbg";
		else
			return version + "-" + std::string(app_version_branch) + std::string(app_version_gitrev);
	}

	return version;
}

auto appGitBranch() -> std::string {
	return {app_version_branch};
}

auto appGitRevision() -> std::string {
	return {app_version_gitrev};
}

auto appCompileDate() -> std::string {
	return {timestamp};
}

auto appCompilerVersion() -> std::string {
	return {__VERSION__};
}
