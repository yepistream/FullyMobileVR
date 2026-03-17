# FullyMobileVR

<!--
SPDX-FileCopyrightText: 2026 Marko Kazimirovic <kazimirovicmarko@photone.me>
SPDX-License-Identifier: CC-BY-4.0
-->

FullyMobileVR is an independent fork of Monado focused on Android XR, ARCore integration, and custom runtime UI work.

## Upstream Credit

This project is based on Monado, originally developed by Collabora, Ltd. and the Monado contributors.

- Upstream project: <https://gitlab.freedesktop.org/monado/monado>
- Monado documentation: <https://monado.freedesktop.org/>

## Scope of This Fork

- ARCore-driven runtime configuration flow in the Android OpenXR runtime target.
- Custom ARCore settings UI and persistence plumbing.
- Ongoing independent hand-tracking integration work (current in-repo edits are mainly ARCore-side changes).

Detailed fork differences are documented in [FORK_CHANGES.md](./FORK_CHANGES.md).

## Build Notes

This repository retains Monado's build structure. Typical host + Android prerequisites from upstream still apply (CMake, Android SDK/NDK, Gradle, Python, Vulkan/OpenXR dependencies).

### Optional Absolute Path Inputs

Absolute paths are optional and should only be used for local machine overrides.

- `eigenCMakeDir` (Gradle property):
  Should point to a directory containing `Eigen3Config.cmake`.
  Examples: `/usr/share/eigen3/cmake` or `C:\\vcpkg\\installed\\x64-windows\\share\\eigen3`.
- `pythonBinary` (Gradle property):
  Should point to a Python 3 executable if automatic discovery fails.
  Examples: `/usr/bin/python3` or `C:\\Python312\\python.exe`.
- `MONADO_ARCORE_CONFIG_JSON` (environment variable):
  Optional absolute path to `arcore_config.json` for runtime config loading.
- `XR_RUNTIME_JSON` (environment variable):
  Optional absolute path to an OpenXR runtime manifest during local testing.

If these are not set, build/runtime uses default discovery logic and fallback behavior.

## Licensing

This repository is mixed-license software and keeps upstream licensing intact.

- Existing upstream files keep their original licenses (for example BSL-1.0 and third-party licenses under `LICENSES/` and `src/external/`).
- Contributions marked with:
  `Marko Kazimirovic <kazimirovicmarko@photone.me> ---> ARCore driver integration, Android runtime configuration UI, and fork-specific build plumbing changes.`
  are additionally licensed as `AGPL-3.0-only`.
- Added AGPL license text is available at `LICENSES/AGPL-3.0-only.txt`.

ARCore SDK content in `src/external/arcore-android-sdk` follows Google's ARCore licensing model (Apache-2.0 for source portions with additional terms for specific binaries/interfaces). See that subtree's `LICENSE` file and Google's terms.

## Maintainer

- Marko Kazimirovic <kazimirovicmarko@photone.me>
