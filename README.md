# BO1 Zombies

Decompiled engine code for Black Ops 1 Zombies, so mods can change the engine itself, not just scripts and maps.

![](images/1.webp)

![](images/2.webp)

The iOS port work includes a portable fastfile header decoder, shared by the
engine loader and a standalone inspection tool. The decoder reads explicit
little-endian fields without assuming pointer size or buffer alignment. The
inspector also checks the decompressed size and asset-list counts. Gameplay,
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
bytes. It does not reconstruct assets or establish that a map is playable.

For an iOS library build with an available Apple SDK, select the iOS toolchain
and set `BO1_BUILD_FASTFILE_INSPECTOR=OFF` and `BUILD_TESTING=OFF`. This builds
the decoder, memory and offline-service libraries. App packaging and device testing remain pending.

All 101 FastFiles in the supplied PC installation passed envelope, decompression,
declared-length and asset-list bounds checks. Kino's zone reports 908 assets and
789 script strings. This validates package structure, with loading those assets
into the game still pending.

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

The renderer candidate is BlackOpsARM's Vulkan implementation through
[MoltenVK](https://github.com/KhronosGroup/MoltenVK) on iOS. Its Android surface,
logging, platform services and device-feature assumptions still need adaptation
and testing. [HarkinianPad's implementation notes](https://github.com/chrissotraidis/harkinianpad/blob/main/docs/ios-feasibility-and-implementation-plan.md)
provide an additional reference for mobile audio, input, sandbox paths and Apple
build integration.
