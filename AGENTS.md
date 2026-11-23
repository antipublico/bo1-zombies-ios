# Codex / Astra — BO1 Zombies iOS handoff

This file applies to the whole repository. **Start with [docs/CODEX_NEXT_STEP.md](docs/CODEX_NEXT_STEP.md)** for the live task, status, commands and acceptance criteria.

- Continue work on `port/xmodel-temp-stream-stack` (draft PR #1). **Do not merge, force-push, or modify `main`**. Keep changes reviewable and update PR #1 with results. If Codex requires a separate task branch, create it from `port/xmodel-temp-stream-stack`, not `main`.
- The next goal is **test the iOS development app's IWI texture viewer end to end**. It is not a playable port. Prioritize real execution and artifacts over speculative game-engine implementation.
- Known-good source commit: `459061cc5c89b5df6b9083e302d8ecdf7d5e4882`. All jobs passed in [Actions run 45134044586](https://github.com/antipublico/bo1-zombies-ios/actions/runs/45134044586). The handoff documentation may be a later commit.
- The app can select a user-owned `.ff` or `.iwi` from Files; IWI v13 format 12 (DXT3/BC2) is decoded to RGBA8 and shown as a UIKit image. CI currently tests the decoder and a synthetic 4×4 IWI-to-UIImage bridge, **not the user's actual image opened via the document picker on a physical device**.
- Only use synthetic IWI/fastfile fixtures in commits, tests and screenshots. **Never push copyrighted game archives, `.ff`, `.iwd`, `.iwi`, real decoded texture previews or game assets.** Local user data stays local.
- Do not request or publish Apple IDs, private signing keys, certificates, provisioning profiles, account tokens or secrets. An unsigned IPA from CI cannot be installed without a separate user-controlled signing process.
- Work on real execution validation: iOS simulator UI screenshot of the texture, regression tests, CI diagnostics; physical-device testing only when a signing-capable device/workflow is actually available. Be precise about what was and was not tested.


- Next milestone after the first physical-phone XModel+IWI render: a diagnostic **nine-instance staged scene**, combining up to six first-LOD0 meshes extracted from separate user-selected FF files. Placement and ground are synthetic, never described as BO1 map geometry. Check `docs/CODEX_NEXT_STEP.md` and fresh CI artifacts for `scene-preview.png`; keep PR draft and main untouched.


- Latest next milestone: diagnostic fullscreen SceneKit first-person camera (touch held arrows + pan-to-look) over **synthetic placements of real LOD0 models**. Original Zombies movement, gameplay and collisions are not implemented. Simulator preboot overlaps builds to reduce Actions duration; check fresh run and screenshots. Loading second XModel within the same FF is still blocked by unresolved recursive material/child stream serialization and cannot be skipped without decoding.


- **CI blocker (2026-11-23):** GitHub Actions jobs currently fail instantly with `runner_id=0` and zero steps on macOS and Windows, including `45154078512` and `45154723996`. This is not a compiler diagnostic. Do not claim the new first-person code passed CI until a new runner actually executes it. The last confirmed green build is `45150908043`, before the first-person additions. New full iOS runs require **workflow_dispatch** or an `[ios ci]` commit-message opt-in, to preserve private-repo macOS minutes. Do not enable billing or change user spending limits.
