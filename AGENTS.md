# Codex / Astra — BO1 Zombies iOS handoff

This file applies to the whole repository. **Start with [docs/CODEX_NEXT_STEP.md](docs/CODEX_NEXT_STEP.md)** for the live task, status, commands and acceptance criteria.

- Continue work on `port/xmodel-temp-stream-stack` (draft PR #1). **Do not merge, force-push, or modify `main`**. Keep changes reviewable and update PR #1 with results. If Codex requires a separate task branch, create it from `port/xmodel-temp-stream-stack`, not `main`.
- The next goal is **test the iOS development app's IWI texture viewer end to end**. It is not a playable port. Prioritize real execution and artifacts over speculative game-engine implementation.
- Known-good source commit: `459061cc5c89b5df6b9083e302d8ecdf7d5e4882`. All jobs passed in [Actions run 45134044586](https://github.com/antipublico/bo1-zombies-ios/actions/runs/45134044586). The handoff documentation may be a later commit.
- The app can select a user-owned `.ff` or `.iwi` from Files; IWI v13 format 12 (DXT3/BC2) is decoded to RGBA8 and shown as a UIKit image. CI currently tests the decoder and a synthetic 4×4 IWI-to-UIImage bridge, **not the user's actual image opened via the document picker on a physical device**.
- Only use synthetic IWI/fastfile fixtures in commits, tests and screenshots. **Never push copyrighted game archives, `.ff`, `.iwd`, `.iwi`, real decoded texture previews or game assets.** Local user data stays local.
- Do not request or publish Apple IDs, private signing keys, certificates, provisioning profiles, account tokens or secrets. An unsigned IPA from CI cannot be installed without a separate user-controlled signing process.
- Work on real execution validation: iOS simulator UI screenshot of the texture, regression tests, CI diagnostics; physical-device testing only when a signing-capable device/workflow is actually available. Be precise about what was and was not tested.
