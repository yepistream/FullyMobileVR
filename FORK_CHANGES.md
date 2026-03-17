# Fork Changes - FullyMobileVR

<!--
SPDX-FileCopyrightText: 2026 Marko Kazimirovic <kazimirovicmarko@proton.me>
SPDX-License-Identifier: CC-BY-4.0
-->

This document summarizes key differences between upstream Monado and this fork snapshot.

## Additions and Modifications

- ARCore-focused Android runtime configuration UI (activity + fragments + resource wiring).
- ARCore config storage and parsing path for `arcore_config.json` in Android runtime code.
- Android driver-side ARCore configuration handling updates (camera mode/options and runtime loading behavior).
- Build integration updates for ARCore-related Android target configuration.

## Attribution and License Metadata

- Added maintainer credit marker in touched files:
  `Marko Kazimirovic <kazimirovicmarko@proton.me> ---> ARCore driver integration, Android runtime configuration UI, and fork-specific build plumbing changes.`
- Added `AGPL-3.0-only` license text under `LICENSES/AGPL-3.0-only.txt`.
- Kept original upstream and third-party licenses in place.

## Repository Cleanup for Publishing

Removed local/reference/generated content that should not be pushed as source:

- `Bug_Fixes_ONLY_A_REFRENCE/`
- `For_UI_Integration/`
- `build/`
- `build-codex/`
- `src/xrt/targets/openxr_android/.cxx/`
- `src/xrt/targets/openxr_android/build/`
- Local IDE/build state folders (`.idea/`, `.gradle/`, `.kotlin/`)
- Local machine config and notes (`local.properties`, `ARCORE_CONFIG_SETTINGS.txt`, `arcore_config.json`)
- Original Git metadata (`.git/`) removed to decouple from upstream repository wiring.

## Notes

- Upstream Monado remains the technical base and should be credited in derivative redistributions.
- This file is a high-level summary, not a commit-by-commit changelog.
