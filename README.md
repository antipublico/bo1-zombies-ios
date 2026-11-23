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
GitHub Actions also builds a Windows x64 inspector artifact
(`bo1-fastfile-inspector-windows-x64`) without bundling copyrighted game data.
Extract the downloaded artifact ZIP, then on Windows, from PowerShell run:

```powershell
.\bo1_fastfile_inspect.exe --load-assets "C:\path\to\your\zone.ff" *> bo1-report.txt
```

Share the small `bo1-report.txt` for diagnosis instead of uploading game archives.
The tool reads the selected file and does not modify or upload it.
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

`LoadAssetPrefix` now reconstructs RawFile, StringTable, LocalizeEntry and
MaterialTechniqueSet objects
using shared engine structure definitions for 32-bit and 64-bit ABIs. Owned
storage keeps pointers valid across copies and moves of the result. It resolves
prior block-4 string references, directory-root aliases and `-2` inserted alias
slots. It stops at the first unsupported root without skipping its body, and
validates the entire compressed stream before publishing the result. Block-0
temporary headers, block-4 alignment and sequential file offsets stay separate.
No scripts are executed and no global engine registry is modified.

Technique sets decode all 130 slots, variable-length techniques with multiple
passes, vertex declarations, vertex/pixel SM3 programs, argument unions and
four-float literal constants. A typed map resolves previously decoded child
objects at their original Win32 block-4 offsets, preserving shared identities
within and across sets. Use `TechniquePassAt` to access the trailing pass storage.
Disk pointer tags never become native pointers directly. Serialized D3D handles
are cleared and vertex declarations remain unloaded: loading these CPU objects
does not translate or execute their shaders on an iOS GPU.

The native loader completed all roots in 12 of the 101 local packages and loaded
nonempty prefixes from another 62. Across those packages it reconstructed 311
rawfiles, 10 tables with 12,522 cells, 19,171 localized entries, and 67 technique
sets. Those sets contain 424 distinct techniques, 424 passes, 166 vertex shaders,
346 pixel shaders and 304,737 shader words, deduplicated within each package
(counts still include entries repeated across packages). The remaining packages stopped before an
unsupported first root. All 101 complete compressed streams passed validation.
Kino's first table has 773 rows and two columns; its next two technique sets
contain no inline techniques. Loading now stops before root 3, an xmodel.
Other local packages exercised actual shader programs and shared child objects.
Native types still to implement include materials,
images, models, animation, collision and sound.

The next Kino model has 12 surfaces, four LODs and three collision surfaces.
The portable loader now decodes the 252-byte Win32 XModel root header and its
fixed skeletal child arrays (bone-name ID bytes, parents, quaternions,
translations, classifications and base matrices) as **diagnostic metadata**,
then reads the contiguous 68-byte XSurface header array (if present) and its
nested inline vertex/index/rigid-list/collision-tree payloads into CPU-only
buffers. It then reads the original MaterialHandle pointer-slot table and
classifies inline/shared/empty Win32 tags. The Windows CLI also opts into a
strictly bounded 192-byte preview of the first inline Material header and
its name. If its technique set is not nested inline, the inspector also
reads the fixed 16-byte MaterialTextureDef records and reports each texture
semantic and serialized GfxImage/water pointer. If the first recursive
child is an inline image (rather than water), the Windows inspector also
decodes that image's 52-byte header and name, including dimensions, mip count,
pixel sizes and pointer tags. For inline -1 or -2 GfxTextureLoad pointers, the inspector
also decodes the 12-byte GfxImageLoadDef header (format, flags, resource length).
A -2 pointer reserves one 4-byte block-4 alias slot without consuming extra
compressed FILE bytes, following DB_InsertPointer in the original loader.
Neither path reads or allocates pixel payload bytes. Inline water payloads,
texture constants, state bits and other Material children remain unresolved.
No texture is uploaded to a GPU.

The first real streaming image `~-gt5_foliage_desert_brush02_c` has
512x512 dimensions with GfxTextureLoad=-2, texture resource length 0 and
Direct3D format 861165636 (= FourCC `DXT3`, BC2). A zero-length inline
resource is not a corrupted format; the actual pixel source remains unknown.
The game's original `Image_LoadFromFile` looks for the image at
`images/~-gt5_foliage_desert_brush02_c.iwi` (potentially inside a
`.iwd` archive loaded by the game's virtual file system). The portable
`IwiHeader` decoder reads the 48-byte `IWi` version-13 header, dimensions,
flags and 8 picmip file-size fields. IWI file format 12 means DXT3/BC2.
After extracting your own IWI into a local folder, inspect or convert its
highest-resolution **2D BC2** mip into an uncompressed preview:

```powershell
.\\bo1_fastfile_inspect.exe --inspect-iwi "C:\\path\\to\\image.iwi"
.\\bo1_fastfile_inspect.exe --decode-iwi "C:\\path\\to\\image.iwi" "preview.tga"
```

The iOS development app now also accepts the user's .iwi image via the
**Abrir textura .iwi** picker. For valid 2D IWi version-13/DXT3 files,
it decodes the highest-resolution mip using the shared BC2 decoder, creates
a premultiplied RGBA8 CoreGraphics image with original transparency, and
previews that image in UIKit. The picker reads only the 48-byte header and
the required top-mip blocks, via scoped/coordinated local file access.
A synthetic .iwi file exercises this same file-to-UIKit bridge in the
ARM64 native self-test. Other IWI formats and 3D/cubemap textures are
rejected without falling back to incorrect decoding. This is a texture
viewer, **not** Metal rendering or playable Zombies.

The IWI decoder never changes the original game file. Cubemaps/volumes, other
image codecs and loaded game engine materials are not supported by this
diagnostic pathway.

The standalone `bo1::texture::DecodeBC2RGBA` CPU decoder can turn a
properly sized BC2 block buffer into RGBA8 with bounded output, clipped
edge blocks, and four-bit alpha. For 512x512 BC2 this would require
262,144 compressed bytes and yield 1,048,576 RGBA8 bytes. This decoding
is covered by independent CTest fixtures and an ARM64 iOS native self-test.
It cannot reconstruct missing streamed pixels and does not render a scene. Shader execution and physics integration are pending.
`AssetPrefix::pendingXModel` retains the counts, tags and four LOD descriptors
without casting serialized pointers to ARM64 pointers. The iOS inspector
reports these values and an estimate of fixed-size inline child arrays.
No model is published as a reconstructed native asset at that stopping point.
Skeletal bytes are retained without resolving script-string IDs or arbitrary
external alias offsets. XSurface header counts and flags are retained, but
inline vertex/index data is retained as original serialized bytes, including
collision nodes/leaves, without any renderer or GPU handles. Referenced
(shared) child arrays remain unresolved, and materials, shaders and gameplay
physics are still not loaded by this preview. The compressed stream and checksum are still validated before
the preview is returned.

Preparatory native helpers also decode the 68-byte Win32 XSurface header
and calculate the consecutive inline blend, tension, vertex, rigid-list and
triangle-index byte counts. They do not allocate these arrays or translate D3D
handles. The nested TEMP stream cursor enforces a 64-frame stack and bounded
alignment, but actual nested XModel/Material operations have **not** yet
been wired to that stack. Completing XModel still requires child pointer
resolution, correct stream-block alignment, 16-byte packed-vertex alignment,
material/image decoding and a representation for bone script-string IDs.
Geometry uploads and physics creation remain separate from CPU reconstruction.

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
[technique graph validation run](https://github.com/antipublico/bo1-zombies-ios/actions/runs/45104724940)
passed on 2026-11-23 at commit `30bba6c`. All eight host tests passed with AddressSanitizer
and UndefinedBehaviorSanitizer on the C++ components. Xcode built and linked the
ARM64 device executable with an iOS 15.0 deployment target, then the app passed
its native component, synthetic-fastfile, text asset and technique graph reconstruction checks
in an iPhone 16e simulator running iOS 26.2. Startup and synthetic-asset result
screenshots were inspected. The run provides six
static libraries, an unsigned development IPA, and simulator logs/screenshots.
The text asset suite includes 76 checks, including invalid dimensions, block
bounds, pointer alias identity, lifetime after copies/moves, byte limits and a
late stream-checksum failure. Shared runtime type layouts also compiled for
Windows x86. Full-engine integration and execution on the physical iPhone SE
remain pending.

The added technique suite has 91 host checks and a shared iOS fixture: multiple
passes, slot 129, typed backward aliases, inserted root aliases, shared literals,
program tokens, cleared GPU handles, lifetime after copies/moves, exact byte
budgets and late checksum failures. All eight host suites passed locally with
the technique loader; it also compiled for Windows x86 and passed a warning-free
x64 compile. The Apple SDK build and simulator validation also passed in the run above.

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
asset fixtures. It checks pointer ownership, alias identity, text assets and the
complete synthetic technique graph. It captures startup and synthetic-result screenshots and publishes
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

## Primer visor 3D de geometría auténtica (desarrollo, iOS)

El lector CPU reconstruye el primer XModel LOD0 soportado de un archivo `.ff` propio del usuario: posiciones float, triángulos indexados y coordenadas de textura T5 empaquetadas como mitades IEEE-754 (orden VU). Rechaza skinning, referencias de geometría faltantes, índices fuera de rango y valores no finitos. La aplicación usa **SceneKit** para mostrar la geometría del juego y aplica la **imagen `.iwi` que seleccionó el usuario** como textura sobre todas las superficies soportadas (no está implementada la vinculación automática de materiales).

En el iPhone, copiá el `.ff` y su `.iwi` a Archivos. Abrí primero `Seleccionar archivo .ff` y después `Abrir textura .iwi` (también se pueden cargar en orden inverso). Si el LOD0 del primer modelo está completo, aparecerá la **vista 3D girable con el dedo** y un botón para alternar con la textura plana. Si no está disponible, la app muestra el motivo en los diagnósticos y mantiene el visor 2D. Es una primera vista de geometría, **no una partida**, y el material no corresponde necesariamente a todas las superficies ni al resto del mapa.

CI valida el decodificador portátil con fixtures sintéticos y el pipeline iOS con `--fixture-model-preview` (captura `model-preview.png`). Esta captura **no usa assets originales del juego**; probar la geometría real sigue requiriendo que el usuario seleccione su propio `.ff` en un iPhone físico. La app se sigue distribuyendo como IPA de desarrollo sin firmar.

## Escena de diagnóstico con varias plantas (2026-11-23)

La aplicación puede **repetir el XModel LOD0 auténtico** ya extraído de un `.ff` sobre una superficie neutra para mostrar **nueve instancias 3D** girables; esas posiciones y el suelo se generan artificialmente para la vista de desarrollo: **no corresponden a un mapa original de Black Ops**. La opción **«Ver escena (9 plantas)»** aparece después de seleccionar un `.ff` con malla soportada y una textura `.iwi`. Tocá **«Ver modelo individual»** para regresar a la vista original y **«Ver textura plana»** para mostrar la imagen.

Opcionalmente, **«Añadir otro modelo .ff»** permite incorporar hasta **seis fuentes** (una malla extraída por cada archivo `.ff` seleccionado; todavía no se puede leer el segundo o tercer XModel de un mismo `.ff`). Las nueve instancias usan esas fuentes de forma alternada; por ahora todas comparten la textura IWI elegida manualmente. Se pueden cargar archivos `.ff` adicionales de manera consecutiva, pero no se guardan permanentemente en la app y ninguna fuente de juego se sube a GitHub. El selector principal **«Seleccionar archivo .ff»** inicia una escena nueva.

CI incluye la prueba independiente y acotada `scene_layout`, un fixture visual de dos geometrías sintéticas y nueve instancias con suelo, además de una captura `scene-preview.png` de SceneKit. Para evaluar el resultado real sigue siendo necesaria una prueba en el dispositivo con archivos propios. No es un juego, nivel, mapa ni motor Zombies completo.

## Exploración en primera persona (iOS, fase experimental)

El escenario de diagnóstico con nueve instancias ahora incluye **«Explorar caminando»**. Desde la escena abierta, tocá el botón para entrar a una vista **a pantalla completa**: mantené presionadas las cuatro flechas para caminar y deslizá un dedo fuera de la cruceta para mirar alrededor. «Volver al visor» cierra la cámara de exploración. La velocidad y los límites de desplazamiento se ajustan a las dimensiones de las mallas importadas; el suelo y la distribución son inventados. **No hay físicas, colisiones, zombis, disparos ni movimiento del motor original de BO1.**

La prueba de CI añade `--fixture-explore-preview`, que crea una escena artificial de dos mallas fuente, presenta la interfaz a pantalla completa, comprueba el nodo de cámara y los cuatro controles y guarda `explore-preview.png/json` (no realiza pruebas automatizadas de gestos reales). La app ARM64 sigue distribuyéndose sin firmar para instalación local con una cuenta de Apple del usuario.

Para reducir esperas de CI, se inicia el simulador macOS en paralelo con la compilación iOS; el script `tools/ios_simulator_smoke.py` acepta opcionalmente `--prebooted-udid` y conserva el arranque independiente cuando ese parámetro no está.
