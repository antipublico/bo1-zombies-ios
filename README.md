# BO1 Zombies

Decompiled engine code for Black Ops 1 Zombies, so mods can change the engine itself, not just scripts and maps.

![](images/1.webp)

![](images/2.webp)

The iOS port work includes a portable fastfile header decoder, shared by the
engine loader and a standalone inspection tool. The decoder reads explicit
little-endian fields without assuming pointer size or buffer alignment. The
inspector also decodes script strings and the asset directory with bounded
retained metadata and streaming decompression. Gameplay,
rendering, audio and asset reconstruction still require platform work.

With CMake and a C/C++ compiler installed, build and test the portable components
from the repository directory. The inspector integration tests require Python 3.9 or newer.

```sh
cmake -S . -B build/portable -DBO1_PORTABLE_TOOLS=ON
cmake --build build/portable --config Release
ctest --test-dir build/portable -C Release --output-on-failure
```

Run `bo1_fastfile_inspect <path-to-zone.ff>` from the build output directory.
It reads the package header and checks the complete compressed stream using the
engine's bundled zlib. Authenticated packages follow the existing loader's
unsupported path. The output reports declared sizes and actual decompressed
bytes, decoded string counts and asset types. It does not reconstruct assets or
establish that a map is playable. Offset strings must reference an already
decoded string in block 4. The API rejects unsupported references explicitly.

For an iOS library build with an available Apple SDK, select the iOS toolchain
and set `BO1_BUILD_FASTFILE_INSPECTOR=OFF` and `BUILD_TESTING=OFF`. This builds
the portable libraries, including the reusable inspection API and bytecode readers.

All 101 FastFiles in the supplied PC installation passed envelope, decompression,
declared-length, script-string and asset-directory checks. Kino's zone reports
908 assets and 789 script-string slots (788 present and one null). Its metadata
consumes 24,399 decompressed bytes. Loading those assets into the game remains pending.

Configure the desktop engine with `-DBO1_WITH_STEAM=OFF` to link the static
unavailable-service backend. It activates the engine's existing offline paths.
The adapter reports unavailable services and keeps SDK callbacks inactive. Its
42 checks passed on Windows x86 and x64. The complete engine build remains
unverified with this option.

The portable `bo1_narrow32` library adapts the region-handle implementation from
[BlackOpsARM](https://github.com/ReeceKetley/BlackOpsARM/tree/2c1edb8720464113eb76d4d0914a6bf959e7136e).
It preserves four-byte VM slots with native 64-bit addresses. Tests passed for
real stack/heap addresses, eight-thread concurrent registration and table
exhaustion. Integration into the engine's VM and expanded asset loader is pending.
The table retains up to 255 distinct 16 MB address regions for the process lifetime.

The manually triggered workflow in `.github/workflows/portable-ios.yml` runs
host tests and builds all three iOS libraries on a macOS runner. The
[first iOS validation run](https://github.com/antipublico/bo1-zombies-ios/actions/runs/44935688430)
passed on 2026-11-21 at commit `77983dd`. All four host tests passed, and Apple
Clang compiled the decoder, memory and offline-service libraries with the
`iphoneos` SDK for ARM64 and an iOS 15.0 deployment target. The run provides
the `bo1-ios-arm64-libraries` artifact with three static libraries. This validates
these components; full-engine integration, app linking, packaging and iPhone
execution remain pending.

The VM and compiler now share explicit little-endian scalar operand reads and
writes. `Scr_ReadUnsigned` consumes four bytes on both x86 and ARM64, and packed
scalar operands no longer require aligned C++ objects at the converted sites.
The same reader functions have 223 checks on Windows x86 and x64. VM pointer
relocation, vectors, other native-width assumptions and the full engine build
remain pending; this is not yet a working 64-bit VM.

The `BO1_BUILD_IOS_APP` target is a UIKit development app that links the native
components and inspects a user-selected `.ff` file. It does not launch gameplay.
The file stays at the selected location and is read using scoped access. The
default limits are 512 MiB of input and decompressed data and 8 MiB of retained
metadata (excluding allocator overhead). No game data is bundled or uploaded.

The workflow also builds the device app, packages `BO1Zombies-unsigned.ipa`, and
runs `--self-test` in an iPhone simulator using a synthetic fastfile. It captures
a startup screenshot and publishes simulator logs as separate artifacts. The
IPA must be signed before device installation; simulator success does not prove
execution on an actual iPhone. Local Mac build commands follow the
[CMake iOS toolchain configuration](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-for-ios-tvos-visionos-or-watchos):

```sh
cmake -S . -B build/ios -G Xcode -DBO1_BUILD_IOS_APP=ON \
  -DBO1_BUILD_FASTFILE_INSPECTOR=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build build/ios --config Release
```

The renderer candidate is BlackOpsARM's Vulkan implementation through
[MoltenVK](https://github.com/KhronosGroup/MoltenVK) on iOS. Its Android surface,
logging, platform services and device-feature assumptions still need adaptation
and testing. [HarkinianPad's implementation notes](https://github.com/chrissotraidis/harkinianpad/blob/main/docs/ios-feasibility-and-implementation-plan.md)
provide an additional reference for mobile audio, input, sandbox paths and Apple
build integration.
