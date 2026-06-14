# Building Limo (no-sudo, from source)

This fork builds without any system-wide installation of libloot or libunrar.
Both are optional and are **off by default** via CMake flags.
The only dependency that is not yet packaged widely enough to pull straight from apt/pacman is **cpr**,
which you build into a local prefix.

---

## Dependencies

Install these from your distro's package manager (names shown for Debian/Ubuntu; Arch equivalents
are in `base-devel` / the standard repos):

| Library | Debian/Ubuntu package |
|---|---|
| Qt 5 (Widgets, Svg, Network) | `qtbase5-dev qtbase5-dev-tools qt5-qmake libqt5svg5-dev` |
| jsoncpp | `libjsoncpp-dev` |
| libarchive | `libarchive-dev` |
| pugixml | `libpugixml-dev` |
| OpenSSL / libssl | `libssl-dev` |
| lz4 | `liblz4-dev` |
| zstd | `libzstd-dev` |
| zlib | `zlib1g-dev` |
| libcurl (used by cpr) | `libcurl4-openssl-dev` |
| Build tools | `cmake ninja-build g++-14 pkg-config git` |

On Arch / CachyOS most of these are in the standard repos under their short names (`qt5-base`,
`jsoncpp`, `libarchive`, `pugixml`, `openssl`, `lz4`, `zstd`, `zlib`, `curl`).

---

## Build cpr from source into a local prefix

cpr is not packaged on Debian 12 / Ubuntu 22.04 in a version new enough for this project.
Build and install it into `/tmp/cpr-prefix` (or any path you prefer):

```bash
git clone --depth 1 https://github.com/libcpr/cpr /tmp/cpr
cmake -S /tmp/cpr -B /tmp/cpr/build -G Ninja \
  -DCPR_USE_SYSTEM_CURL=ON \
  -DCPR_BUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_INSTALL_PREFIX=/tmp/cpr-prefix
cmake --build /tmp/cpr/build --target install
```

---

## Configure and build Limo

```bash
git clone https://github.com/<your-fork>/limo.git
cd limo

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIMO_WITH_LOOT=OFF \
  -DLIMO_WITH_UNRAR=OFF \
  -DCMAKE_PREFIX_PATH=/tmp/cpr-prefix

cmake --build build -j"$(nproc)"
```

### CMake options

| Option | Default | Effect |
|---|---|---|
| `LIMO_WITH_LOOT` | `OFF` | Compile the LOOT and OpenMW deployers (Bethesda / Morrowind plugin sorting). Requires libloot (`loot/api.h` + `libloot.so`). |
| `LIMO_WITH_UNRAR` | `OFF` | Compile RAR-archive extraction support. Requires libunrar headers and either `libunrar.a` or `libunrar.so`. |
| `LIMO_INSTALL_PREFIX` | `/usr/local` | Where `cmake --install` places binaries, icons, and `steam_app_configs/`. |
| `IS_FLATPAK` | `OFF` | Set by the Flatpak build manifest; paths are routed under `/app/`. |

When both optional flags are `OFF` you do **not** need libloot, libunrar, or any Rust toolchain.
The LOOT and OpenMW deployer source files are excluded from the build entirely
(see `CMakeLists.txt`, lines 195-203).

---

## Running without installing

The cpr shared library is not on the system library path, so point `LD_LIBRARY_PATH` at the prefix:

```bash
LD_LIBRARY_PATH=/tmp/cpr-prefix/lib ./build/Limo
```

---

## Optional: run the tests

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIMO_WITH_LOOT=OFF \
  -DLIMO_WITH_UNRAR=OFF \
  -DCMAKE_PREFIX_PATH=/tmp/cpr-prefix \
  -DBUILD_TESTING=ON

cmake --build build -j"$(nproc)"
ctest --test-dir build
```

---

## CI reference

The CI workflow (`.github/workflows/ci.yml`) runs exactly the steps above on Ubuntu 24.04 with
`g++-14`. If a build fails locally but passes CI (or vice versa), that file is the canonical
reference for the expected compiler and flag set.
