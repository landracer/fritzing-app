<div align="center">

# 🥚 Fritzing

**A modernized, actively-maintained fork-of-record of the original Fritzing EDA.**

*Sketch a circuit on a real-looking breadboard → turn it into a PCB ready for fab — with a low entry
barrier made for makers, students, and Arduino/Raspberry Pi tinkerers.*

[![Qt](https://img.shields.io/badge/Qt-6.x%20floating-41cd52?logo=qt&logoColor=white)](#-prerequisites)
[![Language](https://img.shields.io/badge/C++-17-blue?logo=cplusplus&logoColor=white)](#-build-out-of-tree-always)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#-build-out-of-tree-always)
[![Tests](https://img.shields.io/badge/tests-10%20suites%20green-brightgreen)](#-build--run-the-automated-tests)
[![License](https://img.shields.io/badge/license-GPLv3%20%2B%20CC%20BY--SA--3.0-lightgrey)](#-licensing)

**Original project:** [`fritzing/fritzing-app`](https://github.com/fritzing/fritzing-app) (read-only upstream we cherry-pick from)
· **Parts library:** [`fritzing/fritzing-parts`](https://github.com/fritzing/fritzing-parts)

</div>

---

Fritzing is an Electronic Design Automation tool with a low entry barrier, suited for the needs of makers and
hobbyists. It offers a real-life "breadboard" view and a parts library of commonly used high-level components,
so it is easy to communicate about a circuit and then turn it into a PCB layout ready for production. It is
particularly popular among Arduino and Raspberry Pi users and is widely used in education and tinkering.

## ✨ What is different in this fork

* **Modern Qt 6.x, version-floating.** The app builds against a current Qt 6 (e.g. 6.11.x) and is **no longer
  pinned** to an old Qt point release. The build keys off the Qt *major* version, so it tracks modern Qt
  without a hard version ceiling.
* **PCB panelizer** and a standalone **Gerber preview** (opt-in beta feature) are first-class.
* **Breadboard autorouter** — auto-place parts and route the ratsnest into clean jumper wires, with
  in-toolbar tuning sliders.
* **A green, real test suite.** 10 automated suites (Boost.Test + QtTest) cover SVG parsing, text utilities,
  gerber export (legacy + improved), the ngspice simulator bridge, project properties, the panelizer /
  gerber-preview widgets, and the breadboard route graph / routing score.

## 🚀 Quick start

```sh
git clone https://github.com/landracer/fritzing-app.git
cd fritzing-app
git checkout develop                                   # canonical line
mkdir build && cd build && qmake6 ../phoenix.pro && make -j"$(nproc)" && ./Fritzing
```


## ⬇️ Download a prebuilt build

Prebuilt artifacts are published on the [**Releases**](https://github.com/landracer/fritzing-app/releases) page as
the `LANDRACER's-BETA-IHv01` beta line.

**Linux (x86_64)** — grab the `Fritzing-LANDRACER-BETA-IHv01-linux-x86_64.tar.gz` asset, then:

```sh
tar xzf Fritzing-LANDRACER-BETA-IHv01-linux-x86_64.tar.gz
cd Fritzing-LANDRACER-BETA-IHv01-linux-x86_64
./fritzing.sh
```

**macOS / Windows** — this fork only publishes a **Linux** artifact here. To run this fork on macOS or Windows,
build from source using the [Prerequisites](#-prerequisites) and [build](#-build-out-of-tree-always) instructions
below, or use an official upstream installer from [fritzing.org/download](https://fritzing.org/download/).


## 📦 Prerequisites

Fritzing links against a fixed set of native libraries. You need **Qt 6** (Core, Gui, Widgets, Xml, Svg,
SvgWidgets, PrintSupport, SerialPort, Sql + the sqlite driver, Core5Compat), plus:

| Library | Used for |
|---------|----------|
| **QuaZip** (built for Qt 6) | the `.fzz` compressed project format |
| **Boost** (headers) | geometry / algorithm support in the autorouter |
| **libgit2** | versioned parts handling |
| **OpenSSL 3** | crypto / signing |
| **ngspice** (shared lib) | circuit simulation |
| **svgpp** (header-only) | SVG parsing |

### 🐧 Ubuntu / Debian (≥ 24.04, Qt 6.4+)

```sh
sudo apt update
sudo apt install -y \
    build-essential qmake6 git \
    qt6-base-dev qt6-svg-dev qt6-serialport-dev qt6-5compat-dev qt6-tools-dev \
    libquazip-qt6-dev libboost-dev libgit2-dev libssl-dev libngspice0-dev
```

> QuaZip-Qt6 and `libngspice0` packaging varies by Ubuntu release. If `libquazip-qt6-dev` is not packaged on
> your release, build QuaZip for Qt 6 from [source](https://github.com/stachenov/quazip) and point
> `pri/quazipdetect.pri` at it. At runtime the app searches for the ngspice shared library in standard dirs
> (`/usr/lib`, `/usr/lib64`).

### 🎩 Fedora / RHEL / CentOS Stream

```sh
sudo dnf install -y \
    qt6-qtbase-devel qt6-qtsvg-devel qt6-qtserialport-devel qt6-qt5compat-devel qt6-qttools-devel \
    quazip-qt6-devel boost-devel libgit2-devel openssl-devel ngspice-devel
```

### 🍎 macOS

Recent macOS blocks "unverified" software by default. Install Qt 6 + the native deps with a package manager
(e.g. Homebrew: `qt@6`, `quazip`, `boost`, `libgit2`, `openssl@3`, `ngspice`), build the same way, then on
first launch open **System Settings → Privacy & Security → "Open"** on the Fritzing prompt (or allow downloads
from identified developers).

### 🪟 Windows

Windows builds use the same qmake project (MSVC or MinGW, plus vcpkg for the native deps). The build keying
is platform-aware (`pri/*.pri` already branch on `win32`). To stand up Windows CI, mirror each dep above
through `vcpkg install` and let the `pri/*detect.pri` scripts find them.


## 🔨 Build (out-of-tree, always)

```sh
mkdir build && cd build
qmake6 ../phoenix.pro
make -j"$(nproc)"
./Fritzing
```

The part definitions live in a **separate repository**
([`fritzing/fritzing-parts`](https://github.com/fritzing/fritzing-parts)) checked out into `parts/` as an
**embedded** repository (not a git submodule). QuaZip is located by `pri/quazipdetect.pri`.

## 🧪 Build + run the automated tests

The suites are wired through `tests/auto/auto.pro`. Boost suites run headless (a suite **passes** when it
prints "No errors detected"); QtTest suites print `Totals: N passed, 0 failed` and need `-platform offscreen`.

> **Boost in test builds:** the leaf suite `.pro` files `include(boostdetect.pri)`, which resolves Boost at
> qmake-parse time. On a box with system Boost, pass it positionally so it is baked into the generated subdir
> make rules: `qmake6 -o Makefile ../tests/auto/auto.pro boost_root=/usr`.

```sh
mkdir build-tests && cd build-tests
qmake6 -o Makefile ../tests/auto/auto.pro boost_root=/usr
make -j"$(nproc)"
# Boost suites: "No errors detected" == pass
./test_svg/test_svg
./test_textutils/test_textutils
./test_gerber/test_gerber              # legacy + improved gerber cases
./test_svg2gerber/test_svg2gerber
./test_ngspice_simulator/test_ngspice_simulator
./test_project_properties/test_project_properties
./test_breadboard_routing_score/test_breadboard_routing_score
./test_breadboard_route_graph/test_breadboard_route_graph
./test_folderutils/test_folderutils
# QtTest suites (headless): print "Totals: N passed, 0 failed"
./test_panelizer/test_panelizer         -platform offscreen
./test_gerberpreview/test_gerberpreview -platform offscreen
```


## 🗂️ Project Structure

* **help** - End-user documentation shipped with the app.
* **parts** - Part definitions (metadata `.fzp` + graphics `.svg`), an **embedded** git repository pointing at
  [`fritzing/fritzing-parts`](https://github.com/fritzing/fritzing-parts) (not a submodule).
* **pri** - qmake `.pri` include files: one per subsystem (items, model, svg, simulation, dock, ...) plus
  dependency-detection files (`quazipdetect.pri`, `boostdetect.pri`, `svgppdetect.pri`, `libgit2detect.pri`,
  `openssl3.pri`, `spicedetect.pri`, `clipper1detect.pri`).
* **resources** - Binaries and definitions not meant to be edited by users (fonts, images, special parts, ...).
* **sketches** - Example circuits/sketches shipped with the application.
* **src** - Application logic. Subsystem dirs mirror `pri/*.pri` (`autoroute`, `gerberpreview`, `svg`,
  `simulation`, `items`, `model`, `mainwindow`, `sketch`, `dialogs`, `partseditor`, `eagle`, `ipc`, `utils`, …).
* **tests/auto** - The automated test suites and their `.pro` wiring (`auto.pro` selects which suites build).
* **tools** - Utility tools for making releases, converting parts, etc.
* **translations** - Language translations.

## 🤝 For contributors & AI agents

If you are picking this project up — human or AI agent — **start at [`AGENTS.md`](AGENTS.md)**. It is the
onboarding contract: what the repo is, how it is built and tested, the conventions to follow, and how to open
a clean slice. The rest of the living documentation lives under [`docs/`](docs/):

| Doc | What it covers |
|-----|----------------|
| [`AGENTS.md`](AGENTS.md) | Onboarding contract: conventions, build/test commands, how to start a session. |
| [`docs/panelizer.md`](docs/panelizer.md) | User-facing panelizer guide. |
| [`docs/design/panelizer-v2.md`](docs/design/panelizer-v2.md) | Panelizer design reference. |
| [`docs/design/gerbview-port-reference.md`](docs/design/gerbview-port-reference.md) | Gerber-preview port reference. |
| [`docs/internal/panelizer-progress.md`](docs/internal/panelizer-progress.md) | Newest-first log of the panelizer/gerber effort. |
| [`docs/internal/issue-triage-log.md`](docs/internal/issue-triage-log.md) | Upstream issues investigated + shot down (root cause + evidence). |
| [`docs/internal/feature-backlog.md`](docs/internal/feature-backlog.md) | Decision-grade triage of deferred upstream feature PRs. |

## 🙏 Credits

The Fritzing app was originally maintained by the Friends-of-Fritzing e.V., a non-profit foundation based in
Berlin, Germany. The project grew out of a state-funded research project at the Interaction Design Lab at
Potsdam University of Applied Sciences. The founding team consisted of Prof. Reto Wettach, Andre Knorig,
Jonathan Cohen, and Stefan Hermann, with many contributors over the years. This fork continues that work as a
standalone fork-of-record, and is written on top of the [Qt](https://www.qt.io) cross-platform framework.

## ⚖️ Licensing

The source code of Fritzing is under **GNU GPL v3**; the documentation and part designs are under **Creative
Commons Attribution-ShareAlike 3.0 Unported**. The full texts of these licenses are shipped with this download.

That's it if your system already has the deps. For a per-distro walkthrough (incl. Ubuntu), keep reading.
