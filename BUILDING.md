# Building 386DX

The build makes these files in `build/`:

| File | What |
| --- | --- |
| `386dx.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `386dx.fwsc` | the installable package (app + loader + JieLi SDK boot files) |

## Prerequisites (macOS)

- Python 3 with Pillow (fonts are rasterised at build time) and NumPy (host quality tests).
  Activate your virtualenv first, or set `PYTHON`.
- Docker (Docker Desktop, or the Docker CLI with Colima and linux/amd64 emulation). The JieLi
  toolchain is Linux x86-64 only, so each tool runs in a `linux/amd64` container. Keep the
  source tree in a folder Docker can share, e.g. under `/Users`.
- CMake, Ninja and a host C compiler (`brew install cmake ninja`). `tools/prepare_espeak.py`
  uses them to build eSpeak NG's native data compiler.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```sh
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

  The script downloads toolchain `20250324.1`, the version releases are built with, and
  checks its SHA-256. JieLi's server also offers newer versions
  (`https://pkgman.jieliapp.com/s/linux-toolchain`). Those are not verified to produce
  identical firmware.

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`). They are not part of this
  tree; `tools/build.py` checks them against the tested SDK version.

  ```sh
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- eSpeak NG 1.52.0 (GPL-3.0-or-later), fetched into this checkout. The build checks the
  revision (`4870adfa25b1a32b4361592f1be8a40337c58d6c`). The checkout itself is never modified;
  `tools/prepare_espeak.py` writes its patched copy and the compiled English data to
  `build/espeak/`.

  ```sh
  git clone --depth 1 --branch 1.52.0 https://github.com/espeak-ng/espeak-ng.git vendor/espeak-ng
  ```

- Node.js (web companion tests) and Emscripten (`emcc`, for the companion's engine,
  `tools/build_web.py`). Both are optional for the firmware alone.

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```sh
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`,
`~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.0` makes a release build and writes `build/release-1.0/` with:

- the package
- the app image
- `SHA256SUMS`
- `SOURCE.txt`, which names the source repository and the pinned tools
- `LICENSE`, `LICENSING.md`, `BUILDING.md` and `LICENSES/`

A release build refuses to run if:

- `DX_LOCAL_DEMOS=1` is set (local-only demo songs)
- a factory song lacks `source` and `license` fields

## Tests

```sh
tests/run_tests.sh               # after ./build.sh
tests/run_tests.sh --host-only   # host tests only, no target build needed
```

## Web companion

```sh
python3 tools/build_web.py       # compiles web/dsp/engine.{mjs,wasm} with Emscripten
```

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```sh
python3 tools/fm1_install.py build/386dx.fwsc
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer starts,
recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Corresponding source

A published package is built from this repository, named in its
`SOURCE.txt`. This repository, the pinned eSpeak NG release, the JieLi toolchain and the JieLi
SDK above are everything needed to rebuild it.
