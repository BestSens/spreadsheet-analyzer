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
- CSV and BeMoS one raw data (`.bin`) files, side by side on a shared time axis
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

## Binary Raw Data

BeMoS one controllers write the waveform level of their measurements as binary files
(`logs_SN…/sync/YYYY_MM_DD/*.bin`, one per ten minutes) next to the CSV log of the top-level
values. Both can be opened through the same entry points; a selection or folder holding both
opens one window per type. The format is documented in the BeMoS one manual, section 11.2
"Rohdatenformat".

A file is a sequence of frames, each made up of a 16 byte header, a JSON metadata field and a
raw data field. All values are stored in network byte order.

| Field | Size | Meaning |
|---|---|---|
| `type` | 1 B | which streams the raw data field holds |
| `dt` | 4 B | spacing between two samples, µs |
| `t0` | 4 B | unix timestamp of the first sample |
| `sM` | 3 B | length of the metadata field |
| `sP` | 4 B | length of the raw data field |

The streams selected by `type` follow each other in equally sized blocks:

| `type` | Streams |
|---|---|
| 0 | sync |
| 1 | ks |
| 2 | sync, integral1 |
| 3 | sync, integral1, integral2, coe |
| 4 | iepe |

`sync` carries two channels per 4 byte slice — the upper 20 bits are the propagation delay (ns),
the lower 12 bits the amplitude (V). `integral1`, `integral2`, `coe` and `iepe` are single
precision floats in V, V, ns and G. `ks` samples are 2 bytes in V.

From each frame's metadata the application additionally builds:

- one column per entry of the controller's `logging_config.data_sources`, carrying its display
  name, unit and decimals, sampled once per frame,
- columns for the frame header values (`board_temp`, `level`, `vga`, `gate`, `temp`, …),
- the DirectView snapshot and the full settings tree, shown in the frame inspector next to the
  plot. The inspector follows the plot cursor, so scrubbing the time axis steps through the
  frames. Toggle it with the waveform button in the window's menu bar.

Frames overlap in time — a frame usually covers more seconds than the interval to the next one.
Later frames win, so the samples of a frame are clipped where the next frame starts.

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open file(s) |
| `Ctrl+Shift+O` | Open folder |
| `Ctrl+Q` | Quit |
| `Shift+Click` | Select a range of columns |
| `Ctrl+Click` | Add or remove a single column (`Cmd+Click` on macOS) |

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
| `FILE` | One or more CSV or `.bin` files to open on startup |
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
| JSON parsing | [glaze](https://github.com/stephenberry/glaze) |
| Float parsing | [fast_float](https://github.com/fastfloat/fast_float) |
| Logging | [spdlog](https://github.com/gabime/spdlog) + [fmt](https://github.com/fmtlib/fmt) |
| File dialogs | [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) |
| CLI parsing | [cxxopts](https://github.com/jarro2783/cxxopts) |
| Fonts | Roboto Sans, Roboto Mono, Font Awesome 6 |

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
