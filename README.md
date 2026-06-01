# Spreadsheet Analyzer

> Fast, hardware-accelerated CSV visualization with intelligent data aggregation.

![Version](https://img.shields.io/badge/version-1.2.1-blue)
![C++](https://img.shields.io/badge/C%2B%2B-23-orange)
![License](https://img.shields.io/badge/license-see%20LICENSE-green)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

Spreadsheet Analyzer is a native desktop application for loading, exploring, and visualizing time-series CSV data. It handles datasets of any size by automatically aggregating data based on the current zoom level — tens of millions of samples render without lag.

---

## Features

**Data Loading**
- Open single files, multiple files, or entire folders at once
- Automatic detection of multiple timestamp formats
- Background loading with progress indicator
- Duplicate any loaded dataset for side-by-side comparison

**Visualization**
- Interactive plots powered by [ImPlot](https://github.com/epezent/implot)
- Shared plot for up to 2 columns, automatic subplot layout for 3 or more
- Cursor annotations showing the exact y-value at the mouse position
- Global X-axis linking to synchronize time ranges across multiple datasets
- Automatic X-axis rescaling when switching between measurements with different time ranges

**Performance**
- Hardware-accelerated rendering via OpenGL and SDL3
- Intelligent data reduction — reduction factors from 1× up to 10,000,000× depending on zoom level
- LTO-optimized release builds

**Usability**
- Native file dialogs on all platforms
- Dark and light theme following the system preference
- Hi-DPI / display scaling aware
- Dockable windows
- Multi-select columns with `Shift` and `Ctrl`
- Configurable maximum displayed data points

---

## Screenshots

> _Place screenshots here._

---

## Supported Date Formats

The first column of a CSV file is expected to contain a timestamp. The following formats are detected automatically:

| Format | Example |
|---|---|
| `YYYY/MM/DD HH:MM:SS` | `2024/03/15 14:30:00` |
| `YYYY-MM-DD HH:MM:SS` | `2024-03-15 14:30:00` |
| `YYYY-MM-DDTHH:MM:SS` | `2024-03-15T14:30:00` (ISO 8601) |
| `DD.MM.YYYY HH:MM:SS` | `15.03.2024 14:30:00` |
| `DD/MM/YYYY HH:MM:SS` | `15/03/2024 14:30:00` |
| `MM/DD/YYYY HH:MM:SS` | `03/15/2024 14:30:00` |

Numeric columns without a timestamp are also supported.

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open file(s) |
| `Ctrl+Shift+O` | Open folder |
| `Ctrl+Q` | Quit |
| `Shift+Click` | Select a range of columns |
| `Ctrl+Click` | Keep current selection |

---

## Usage

Launch the application and open a CSV file via **File → Open** or drag-and-drop. You can also pass files directly on the command line:

```sh
spreadsheet_analyzer data.csv
spreadsheet_analyzer measurement1.csv measurement2.csv
spreadsheet_analyzer --verbose data.csv
```

**Options**

| Flag | Description |
|---|---|
| `FILE` | One or more CSV files to open on startup |
| `-v`, `--verbose` | Enable verbose output and show the debug menu |
| `-h`, `--help` | Print usage |

---

## Building

**Requirements**

- CMake ≥ 3.24
- C++23-capable compiler (GCC ≥ 13, Clang ≥ 17, MSVC 2022)
- OpenGL

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For a debug build with coverage instrumentation (Clang only):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

LTO is enabled automatically for non-debug builds. To disable it:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_LTO=OFF
```

**macOS** produces a `.app` bundle. **Windows** builds include a manifest and version resource.

---

## Tech Stack

| Component | Library |
|---|---|
| GUI framework | [Dear ImGui](https://github.com/ocornut/imgui) |
| Plotting | [ImPlot](https://github.com/epezent/implot) |
| Windowing / rendering | [SDL3](https://github.com/libsdl-org/SDL) + OpenGL |
| Image loading | [SDL_image](https://github.com/libsdl-org/SDL_image) |
| CSV parsing | [csv-parser](https://github.com/vincentlaucsb/csv-parser) |
| Float parsing | [fast_float](https://github.com/fastfloat/fast_float) |
| Logging | [spdlog](https://github.com/gabime/spdlog) + [fmt](https://github.com/fmtlib/fmt) |
| File dialogs | [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) |
| CLI parsing | [cxxopts](https://github.com/jarro2783/cxxopts) |
| Fonts | Roboto Sans, Roboto Mono, Font Awesome 6 |

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
