## 1.2.0 (unreleased)
* fix dispay scaling issues
* create MacOS app bundle
* update implot to v1.0 (https://github.com/epezent/implot/releases/tag/v1.0)
* update SDL to v3.4.8 (https://github.com/libsdl-org/SDL/releases/tag/release-3.4.8)
* update SDL_image to v3.4.4 (https://github.com/libsdl-org/SDL_image/releases/tag/release-3.4.4)
* update csv-parser to v5.3.0 (https://github.com/vincentlaucsb/csv-parser/releases/tag/5.3.0)
* add support for ISO 8601 (`YYYY-MM-DDTHH:MM:SS`), European slash (`DD/MM/YYYY HH:MM:SS`), and US (`MM/DD/YYYY HH:MM:SS`) timestamp formats
* show CSV import configuration dialog on parse failure, allowing per-file configuration of field delimiter, decimal separator, and date format
* make header parsing case-insensitive
* update fast_float to v8.2.5 (https://github.com/fastfloat/fast_float/releases/tag/v8.2.5)
* also allow bracketed units in header parsing, e.g. `Time (s)` or `Time [s]`
* add timezone support to timestamp parsing, e.g. `2025-03-14T15:09:26+01:00` or `2025-03-14 15:09:26Z`
* fix time extends when enabling global x-link the first time
* add menu entry for manual import
* add HowardHinnant/date for portable date parsing

## 1.1.1 (23.03.2026)
* fix generation of unique file titles to work on more circumstances
* update imgui to v1.92.6 (https://github.com/ocornut/imgui/releases/tag/v1.92.6)
* update SDL to v3.4.2 (https://github.com/libsdl-org/SDL/releases/tag/release-3.4.2)
* update SDL_image to v3.4.0 (https://github.com/libsdl-org/SDL_image/releases/tag/release-3.4.0)
* update spdlog to v1.17.0 (https://github.com/gabime/spdlog/releases/tag/v1.17.0)
* update fmt to v12.1.0 (https://github.com/fmtlib/fmt/releases/tag/12.1.0)
* update cxxopts to v3.3.1 (https://github.com/jarro2783/cxxopts/releases/tag/v3.3.1)
* update expected to v1.3.1 (https://github.com/TartanLlama/expected/releases/tag/v1.3.1)
* update fast_float to v8.2.4 (https://github.com/fastfloat/fast_float/releases/tag/v8.2.4)

## 1.1.0 (14.03.2025)
* move configuration of maximum displayed data points to settings menu
* fix crash if reduction factor > sample count
* change shade color to text cursor color when switching to min/max
* use one shared plot for up to two columns, automatically switch to subplots on more
* automatically rescale x axis when changing to another measurement which time range is completely out of view
* add scrollbar to measurement list box
* fix id error when column is present in data set with and without a unit specified
* fix floating point parsing on some locales
* use system locale for anything except file parsing
* show annotations at cursor to show next y-value
* allow duplicating of already loaded data sets
* change global x-link and force subplots to be a individual window settings

## 1.0.0 (05.03.2025)
* initial release