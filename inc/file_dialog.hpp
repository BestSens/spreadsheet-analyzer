#pragma once

#include <filesystem>
#include <vector>

auto selectFilesFromDialog(bool select_folder) -> std::vector<std::filesystem::path>;
