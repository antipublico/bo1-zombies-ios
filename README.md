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
bytes, decoded string counts and asset types. Add `--load-assets` before the path
to reconstruct the supported consecutive asset roots into native engine types.
This does not establish that a map is playable. Offset strings must reference an already
decoded string in block 4. The API rejects unsupported references explicitly.

For an iOS library build with an available Apple SDK, select the iOS toolchain
and set `BO1_BUILD_FASTFILE_INSPECTOR=OFF` and `BUILD_TESTING=OFF`. This builds
the portable libraries, including the reusable inspection API and bytecode readers.

All 101 FastFiles in the supplied PC installation passed envelope, decompression,
declared-length, script-string and asset-directory checks. Kino's zone reports
908 assets and 789 script-string slots (788 present and one null). Its metadata
consumes 24,399 decompressed bytes. Engine registration of reconstructed assets remains pending.

`LoadAssetPrefix` now reconstructs RawFile, StringTable and LocalizeEntry objects
using shared engine structure definitions for 32-bit and 64-bit ABIs. Owned
storage keeps pointers valid across copies and moves of the result. It resolves
prior block-4 string references, directory-root aliases and `-2` inserted alias
slots. It stops at the first unsupported root without skipping its body, and
validates the entire compressed stream before publishing the result. Block-0
temporary headers, block-4 alignment and sequential file offsets stay separate.
No scripts are executed and no global engine registry is modified.

The native loader completed all roots in 12 of the 101 local packages and loaded
nonempty prefixes from another 27. Across those packages it reconstructed 311
rawfiles, 10 tables with 12,522 cells, and 19,171 localized entries (counts include
entries repeated across packages). The remaining packages stopped before an
unsupported first root. All 101 complete compressed streams passed validation.
Kino's first table has 773 rows and two columns; loading stops before root 1,
a technique set. Native types still to implement include shaders, materials,
images, models, animation, collision and sound.

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
host tests and builds the iOS development app on a macOS runner. The
[app validation run](https://github.com/antipublico/bo1-zombies-ios/actions/runs/44937239981)
passed on 2026-11-21 at commit `86c98f1`. All six host tests passed with AddressSanitizer
and UndefinedBehaviorSanitizer on the C++ components. Xcode built and linked the
ARM64 device executable with an iOS 15.0 deployment target, then the app passed
its native component and synthetic-fastfile checks in an iPhone 16e simulator
running iOS 26.2. The startup screenshot was inspected. The run provides six
static libraries, an unsigned development IPA, and simulator logs/screenshots.
Full-engine integration and execution on the physical iPhone SE remain pending.

The VM and compiler now share explicit little-endian scalar operand reads and
writes. `Scr_ReadUnsigned` consumes four bytes on both x86 and ARM64, and packed
scalar operands no longer require aligned C++ objects at the converted sites.
The same reader functions have 223 checks on Windows x86 and x64. VM pointer
relocation, vectors, other native-width assumptions and the full engine build
remain pending; this is not yet a working 64-bit VM.

The `BO1_BUILD_IOS_APP` target is a UIKit development app that links the native
components and inspects a user-selected `.ff` file. It does not launch gameplay.
The file stays at the selected location and is read using scoped access. The
default limits are 512 MiB of input and decompressed data, 8 MiB of accounted
metadata, and 32 MiB of accounted native payload/bookkeeping. Allocator overhead
and container spare capacity are excluded; these are not process RSS limits.
No game data is bundled or uploaded.

The workflow also builds the device app, packages `BO1Zombies-unsigned.ipa`, and
runs `--self-test` in an iPhone simulator using synthetic metadata and native
asset fixtures. It checks pointer ownership, alias identity and all three native
types. It captures startup and synthetic-result screenshots and publishes
simulator logs as separate artifacts. The external document picker and its
security-scoped access still require device or UI testing. The
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
