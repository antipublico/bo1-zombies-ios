# NEXT TASK — Astra / Codex: test the BO1 iOS texture viewer

**Status:** ready for next agent. **Do the tests and fix what fails.** This is a handoff, not a claim that a real iPhone has already been tested.

## Where to work

- Repository: `antipublico/bo1-zombies-ios` (private).
- Working branch: `port/xmodel-temp-stream-stack`; existing **draft PR [#1](https://github.com/antipublico/bo1-zombies-ios/pull/1)**. Leave `main` unchanged.
- Last confirmed green implementation: commit `459061cc5c89b5df6b9083e302d8ecdf7d5e4882` and [workflow run 45134044586](https://github.com/antipublico/bo1-zombies-ios/actions/runs/45134044586) — Windows inspector + all CTests, macOS host sanitizer tests, iPhone ARM64 build, simulator build and native on-simulator self-test all succeeded. Artifacts include unsigned `bo1-ios-development-unsigned` IPA, `bo1-ios-simulator-results`, Windows executable and ARM64 static libs.
- Current development app: `ios/main.mm`, build: `cmake/ios.cmake`, iOS bundle type declaration: `ios/Info.plist.in`, simulator runner: `tools/ios_simulator_smoke.py`, CI: `.github/workflows/portable-ios.yml`.
- Portable assets/texture logic: `src/platform/bo1_iwi_image.h`, `src/platform/bo1_texture_bc.h`, tests `tests/iwi_image_tests.cpp` and `tests/bc2_texture_tests.cpp`. Unit tests currently do **not** drive the actual `UIDocumentPickerViewController` UI.

## Actual user evidence (not files in Git)

The user, on Windows 10, extracted the real file `images/~-gt5_foliage_desert_brush02_c.iwi` from their own BO1 `main/iw_11.iwd`. The real `.iwi` contains IWi version **13**, format **12 = DXT3/BC2**, flags **0x10**, size **349,600 bytes**, dimensions **512×512×1**, picmip byte positions `349600 87456 21920 5536 1440 416 160 96`. The top mip begins at offset **87,456**, with **262,144** BC2 bytes and **1,048,576** RGBA8 output bytes. The Windows `--decode-iwi` emitted `preview.tga` successfully and the user displayed recognizable foliage with transparency.

The `.ff` diagnostic contained the model `t5_foliage_desert_brush03` and material `mc/t5_foliage_desert_brush02_mtl`; its inline `GfxImageLoadDef` had `format=861165636` (D3D FourCC DXT3) and zero inline pixel bytes. The original engine locates the image through `images/<name>.iwi` in its virtual filesystem. **Do not conflate these `.ff` placeholders with missing pixels or claim gameplay is functional.**

## Primary assignment: run the iOS app and actually validate the texture viewer

1. Inspect and understand the existing app and GitHub Actions tests; reproduce `45134044586` or an equivalent fresh run. Build the simulator app and run `python3 tools/ios_simulator_smoke.py build/simulator/Release-iphonesimulator/BO1Zombies.app --output build/simulator-results` on a macOS host with Apple SDK. If your own environment lacks Xcode, use GitHub Actions and inspect its uploaded results; don't claim direct simulator execution you did not perform.
2. **Add a UI-level simulator test for the new IWI preview**, not only the current 4×4 decoder/self-test. Prefer a new explicit CI-only launch flag such as `--fixture-iwi-preview` that generates a synthetic IWi v13 2D DXT3 file and calls the same `IwiPreviewForPath` path. Make the UIKit `UIImageView` visible and capture a screenshot through `simctl io ... screenshot` into `build/simulator-results/iwi-preview.png`. Add a deterministic pass/fail signal or pixel/CGImage/assertion so a blank image cannot pass. Keep the regular user-facing document picker intact.
3. Add smoke checks to `tools/ios_simulator_smoke.py` so the new screenshot and validation are actually exercised by the workflow. Verify the existing `--self-test`, first launch screenshot and `--fixture-preview` still work.
4. Run all host CTests and GitHub Actions on the port branch; fix any compilation, Objective-C++/CoreGraphics ownership, memory safety, path, UI layout, synchronization or simulator failures. Pay attention to the new `Abrir textura .iwi` button, custom `.iwi` type and the CoreGraphics premultiplied-alpha `UIImage`. Record final run URL, success/failure, and artifacts. Do not merge PR #1.
5. **Physical iPhone / actual user's IWI is a follow-on manual test**, not a prerequisite for synthetic CI validation. An actual iPhone and signing credentials are not accessible in GitHub runners. Prepare exact steps for the user to sign the unsigned dev IPA with their own account, transfer their local `foliage.iwi` through Files, tap `Abrir textura .iwi`, and inspect transparency. Don't ask for private keys or upload the game's files. State clearly if real-device verification is still outstanding.
6. Provide a concise Spanish status summary in PR #1 or this issue, including which tests actually ran, screenshots/artifacts, remaining blockers, and the next implementation milestone. Do not claim Metal 3D rendering, Zombies gameplay, sound or controls are working.

## Commands

Portable Windows / macOS host tests:

```sh
cmake -S . -B build/portable -DBO1_PORTABLE_TOOLS=ON
cmake --build build/portable --config Release --parallel 3
ctest --test-dir build/portable -C Release --output-on-failure
```

iOS simulator build and native smoke checks (requires macOS + Xcode):

```sh
cmake -S . -B build/simulator -G Xcode -DBO1_PORTABLE_TOOLS=ON -DBO1_BUILD_IOS_APP=ON -DBO1_BUILD_FASTFILE_INSPECTOR=OFF -DBUILD_TESTING=OFF -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build build/simulator --config Release --parallel 3
python3 tools/ios_simulator_smoke.py build/simulator/Release-iphonesimulator/BO1Zombies.app --output build/simulator-results
```

See `.github/workflows/portable-ios.yml` for both iPhone device and simulator CI build steps.

## Done when

- [ ] A simulator UI test shows an **actual visible synthetic DXT3/BC2 image** using the UIKit rendering path (not just a successful parser return).
- [ ] Logs and a screenshot are uploaded in the GitHub Actions simulator-results artifact.
- [ ] Windows/portable CTests, sanitizers, iPhone ARM64 build and simulator native smoke are all green on the finished code commit.
- [ ] PR #1 is updated with test evidence, exact remaining real-device/signing work, and stays draft.
- [ ] `main` is unchanged. No proprietary game binaries, Apple signing secrets or credentials appear in any commit or artifact.

**Important:** if no Mac/Apple SDK or iPhone signing access is available, finish all automation possible, document the limitation, and give the user the exact minimum step they must perform. No speculative claims of having launched the app on physical hardware.


## Update: first native 3D model viewer (2026-11-23)

Code now includes `src/platform/bo1_model_mesh.h` for safe, CPU-owned first-XModel LOD0 geometry extracted from existing `LoadAssetPrefix` (T5 VU half-float UVs, XSurface packed xyz/u16 indexed triangles) and a SceneKit-based iPhone diagnostic viewer that binds the user-selected DXT3/BC2 IWI to its supported surfaces. The model and texture pickers may be used in either order. The native 3D view supports orbit touch gestures and a switch back to 2D. CI adds CTest `model_mesh`, a synthetic crossed-planes preview `--fixture-model-preview` and screenshot `model-preview.png`. See current branch Actions for outcomes; do not assume success until green. Material selection/texture mapping, full XModel registry, skinning, physics and game rendering are still future work.


### Update: staged multi-model diagnostic scene

The app now supports the 9-instance staged SceneKit layout from `src/platform/bo1_scene_layout.h` and bounded source meshes imported one `.ff` at a time (max 6). `Ver escena (9 plantas)` and `Ver modelo individual` toggle the scene; `Añadir otro modelo .ff` appends the first supported LOD0 model in another user-provided FastFile. Every source currently shares the manually selected IWI, and model placement/ground are *synthetic*, **not recovered BO1 map placements**. CI adds a `scene_layout` CTest and `--fixture-scene-preview` screenshot/marker `scene-preview.png/json` using only synthetic meshes. Check final Actions status before calling green. The first authentic physical-phone scene test remains up to the user.


### Update: first-person staged scene and CI startup overlap

Added `ios/bo1_explore_view.h/.mm`: a distinct fullscreen UIKit+SceneKit controller with four held directional buttons and pan-to-look camera. `ios/main.mm` offers the **Explorar caminando** button only in the 9-instance SceneKit layout. A synthetic `--fixture-explore-preview` run produces `explore-preview.png/json`; marker asserts presentation, camera and four controls but **does not emulate player gestures or original BO1 physics**. Changes to `tools/ios_simulator_boot.py`, `tools/ios_simulator_smoke.py` and workflow preboot the iPhone simulator during two Xcode builds, rather than after, with the objective of reducing total Actions time; measure the actual result. Do not claim all CI green until the new run completes.
