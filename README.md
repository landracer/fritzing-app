# Fritzing

**This is the `landracer` fork-of-record of the Fritzing application** - a modernized,
actively-maintained line of the original project. Upstream
[`fritzing/fritzing-app`](https://github.com/fritzing/fritzing-app) is kept as a read-only
remote that we cherry-pick from; this repository is the canonical line we drive.

The Fritzing application is an Electronic Design Automation tool with a low entry barrier,
suited for the needs of makers and hobbyists. It offers a unique real-life "breadboard" view,
and a parts library with many commonly used high-level components. Fritzing makes it easy to
communicate about circuits, and to turn them into PCB layouts ready for production. It is
particularly popular among Arduino and Raspberry Pi users, and is widely used in education and
creative tinkering.

## What is different in this fork

* **Modern Qt 6.x, version-floating.** The app builds against a current Qt 6 (e.g. 6.11.x) and is
  no longer pinned to an old Qt point release. The build keys off the Qt *major* version, so it
  tracks modern Qt without a hard version ceiling.
* **PCB panelizer** and a standalone **Gerber preview** (opt-in beta feature) are first-class.
* **The full automated test suite builds and passes** against floating Qt 6 (Boost.Test and QtTest
  suites covering SVG parsing, text utilities, gerber export in both the legacy and improved
  formats, the ngspice simulator bridge, project properties, and the panelizer/gerber-preview
  widgets).

## Building from source

You need a **Qt 6** installation (Core, Gui, Widgets, Xml, Svg, SvgWidgets, PrintSupport,
SerialPort, Sql + the sqlite driver, Core5Compat), **QuaZip built for Qt 6**, **Boost**,
**libgit2**, **OpenSSL 3**, **QtSerialPort**, **ngspice** (shared library, for simulation), and
**svgpp**. On a Fedora/RHEL-style box that is roughly:

```sh
sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel qt6-qtserialport-devel \\
                 qt6-qt5compat-devel quazip-qt6-devel boost-devel libgit2-devel \\
                 openssl-devel ngspice-devel
```

QuaZip is located by `pri/quazipdetect.pri`. The part definitions live in a **separate
repository** ([`fritzing/fritzing-parts`](https://github.com/fritzing/fritzing-parts)) that is
checked out into `parts/` as an embedded repository (not a git submodule).

### Configure + build the app (out-of-tree)

```sh
mkdir build && cd build
qmake6 ../phoenix.pro
make -j"$(nproc)"
./Fritzing
```

### Build + run the automated tests

The suites are wired through `tests/auto/auto.pro`:

```sh
mkdir build-tests && cd build-tests
qmake6 ../tests/auto/auto.pro
make -j"$(nproc)"
# Boost suites:
./test_svg/test_svg
./test_textutils/test_textutils
./test_gerber/test_gerber              # holds both the legacy and improved gerber cases
./test_svg2gerber/test_svg2gerber
./test_ngspice_simulator/test_ngspice_simulator
./test_project_properties/test_project_properties
# QtTest suites (headless):
./test_panelizer/test_panelizer -platform offscreen
./test_gerberpreview/test_gerberpreview -platform offscreen
```

## Project Structure

* **help** - End-user documentation shipped with the app.

* **parts** - Part definitions (metadata `.fzp` + graphics `.svg`). Kept in a separate repository
  at [https://github.com/fritzing/fritzing-parts](https://github.com/fritzing/fritzing-parts)
  and checked out into this directory as an embedded repository (not a git submodule).

* **pri** - qmake `.pri` include files: one per subsystem (items, model, svg, simulation, dock, ...)
  plus dependency-detection files (`quazipdetect.pri`, `boostdetect.pri`, `svgppdetect.pri`,
  `libgit2detect.pri`, `openssl3.pri`, ...).

* **resources** - Binaries and definitions not meant to be edited by users (fonts, images, special
  parts, etc.).

* **sketches** - Example circuits/sketches shipped with the application.

* **src** - Application logic.

* **tests/auto** - The automated test suites and their `.pro` wiring (`auto.pro` selects the suites
  that build/run by default).

* **tools** - Utility tools for making releases, converting parts, etc.

* **translations** - Language translations.

## Credits

The Fritzing app was originally maintained by the Friends-of-Fritzing e.V., a non-profit foundation
based in Berlin, Germany. The project grew out of a state-funded research project at the Interaction
Design Lab at Potsdam University of Applied Sciences. The founding team consisted of Prof. Reto
Wettach, Andre Knorig, Jonathan Cohen, and Stefan Hermann, with many contributors over the years.

This fork continues that work as a standalone fork-of-record.

The Fritzing app is written on top of the Qt cross-platform framework.

## Licensing

The source code of Fritzing is under GNU GPL v3; the documentation and part designs are under
Creative Commons Attribution-ShareAlike 3.0 Unported. The full texts of these licenses are shipped
with this download.
