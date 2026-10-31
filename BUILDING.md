# Building the 1.0.3.1 release source

This is 1.0.3.1 release source through test19 with motion/tilt excluded.
The native engine/graphics baseline is test17, followed by the ROM-update Java
changes. The signed release APK is supplied separately from this source archive.

Graphics policy correction: GLES is automatically enabled with GPU preferred;
manual GLES options are removed. The GLES badge recognizes the uncompressed
ARM CE DLL file layout in our raw Device Emulator ROMs, not arbitrary compressed
or physical-device ROM formats. The current focused runner is
python3 android/tests/run_gles_automatic_tests.py.

Current additions:
- android/src: current frontend without motion settings or capture.
- android/assets: exact authored non-motion setup assets from test17.
- qemu-android-exact: GLES/service/import and dirty-display source overlays.
- prototypes/gles-android: GLES1 guest DLL/setup source, selector, GPU renderer
  and Rust software adapter. prototypes/gles-support/bridge-lab/host retains its
  complete local Rust dependency/vendor source and notices.
- prototypes/gles2: shared-memory GLES2 native/guest source with test16 fixes.
- prototypes/d3dm: Direct3D Mobile adapter source.

Run python3 tools/build_graphics.py --target host for native C graphics libraries.
Use --target android with P2D_NDK_BIN pointing at the NDK LLVM bin directory.
The Rust software adapter builds with Cargo in prototypes/gles-android/native;
its path dependency and lockfiles are included. Its Cargo output libp2d_gles.so
must be deployed as libp2d_gles_cpu.so beside the current selector, with a matching
SONAME. For Android set the NDK linker and target Rust flags including
-C link-arg=-Wl,-z,max-page-size=16384 and
-C link-arg=-Wl,-soname,libp2d_gles_cpu.so. Guest DLL builders require LLVM17
ARM tools and the supplied network helper PE builder; no Microsoft SDK binaries,
games or ROMs are required for GLES compilation. The East Story shader mapping
contains SHA-256 fingerprints and authored replacement GLSL only; original game
shader binaries and source text are excluded. Direct3D Mobile adapter builds
separately require matching SDK120 headers supplied by the contributor through
P2D_D3DM_SDK; those Microsoft headers are intentionally not redistributed.
The adapter imports the private p2dGLES2.dll entry point; deployment remains
explicit rather than automatic graphics setup.

The new transaction runner is python3 android/tests/run_rom_update_tests.py
--repository, with --fallback for unsupported-rename publication. Existing
library, geometry and network runners remain available. They use disposable files.

Older scripts/records below are retained provenance with maintainer cache and
signing assumptions, not a complete fresh-checkout APK build. Old binary hashes
describe historical releases. Use a separate contributor app ID/key for device
builds. This release variant is not identical to the motion-enabled signed test19 APK.
No clean-machine full-APK build is claimed.

---

# Historical build pipeline

The v1.0.2 APK was built with an Android Java frontend and an ARM64 QEMU engine compiled for Android's Bionic runtime. This snapshot contains the corresponding source, build recipes and recorded dependency identities.

The archived recipes currently depend on the maintainer's workspace and Linux caches. Building from a fresh checkout requires the portability changes listed below; the source snapshot alone does not yet provide a complete setup command.

## Source layout

| Path | Purpose |
| --- | --- |
| `android/src/com/samsixx/pocket2droid/` | Android activities, library, controls, input mapping and emulator lifecycle |
| `android/native/storage_publish.c` | Native helper for verified file publication |
| `android/res/` | Launcher icon, ROM icons, control artwork and loading GIF |
| `android/tests/` | Frontend, settings, filesystem, input and lifecycle checks |
| `android/build-tooling/` | Shared build-cache and APK-manifest verification helpers |
| `qemu-android-exact/` | QEMU source used by the Android native build, including the Android adaptations |
| `qemu-canonical-changes/` | Pocket2Droid's canonical QEMU changes |
| `tests/` | Native emulation, network, loader and device correctness checks |
| `guest/network-setup/` | ARMv4 Windows CE one-shot network helper source and PE builder |
| `android/build_slirp.py` | Pinned static libslirp host/Android build recipe |
| `android/patches/` | libslirp resolver patch and notices |
| `android/build/networking/android/` | Exact libslirp provenance, source archive, changed source and notices |
| `android/build/network-setup/production-helper/` | Exact guest helper asset and source/tool hash manifest |
| `sources.lock.json` | Upstream revisions and development provenance |

`qemu-android-exact` already contains the Android adaptations. Applying those adaptations a second time is not part of reconstructing the recorded Android source.

## Toolchain used for v1.0.2

| Component | Configuration |
| --- | --- |
| Host | Ubuntu 24.04 under WSL, x86_64 |
| Python | Python 3.12 in the working host environment |
| Java | OpenJDK 17 |
| Android SDK | API 36 platform and Build Tools 36.0.0 |
| Android NDK | r30, Linux x86_64 toolchain |
| Native target | `aarch64-linux-android24` |
| App SDK settings | Minimum API 24; target API 36 |
| Native build | QEMU `arm-softmmu` with the Pocket2Droid device configuration |
| Alignment | 16 KiB ELF load segments and APK native-library alignment |

The build also uses Git, a host C compiler, Ninja, Make, pkg-config, patch, unzip, patchelf and binutils. QEMU configuration manages its Python build dependencies.

The recorded Android dependency identities are in `android/build/bionic-tools.json`. Selected runtime package inputs are in `android/dependency-inputs/`; they are not a complete SDK or development sysroot. Component license files are retained in `android/dependency-licenses/` and the source trees.

## Current maintainer build process

The following scripts describe the existing working pipeline. Their current workspace assumptions must be resolved before using the same pipeline in a fresh checkout.

1. `android/prepare_tools.py` prepares the Android SDK platform/build tools and Java compiler environment.
2. `android/prepare_bionic.py` prepares the NDK and Android library sysroot. The network helper also requires host LLVM 17 tools (`clang-17`, `ld.lld-17` and `lld-link-17`).
3. `python3 android/build_slirp.py --platform android` builds the pinned libslirp dependency before the native engine. Its Android resolver patch and source archive are included.
4. `android/build_bionic.py` compiles the Android QEMU engine from the canonical development checkout and applies the Android adaptations.
5. `android/package_runtime.py` collects the native dependency closure, assigns private library names and checks ELF alignment.
6. `android/build_storage.py` compiles the storage publication helper.
7. `android/build.py` compiles the Java classes and DEX, links Android resources, packages the runtime, aligns the APK and signs it.
8. `android/seal_v1_0_2.py` verifies 1.0.2 and archives its exact source and integrity records. This is a maintainer release tool, not a general contributor build entry point.

`prepare_bionic.py` reads a live Termux package index. Its output should not be assumed to reconstruct the recorded v1.0 dependency versions; those versions and hashes are in `bionic-tools.json`.

## Portability work needed before a fresh-checkout build

### Workspace paths and source synchronization

`sync_source.py` expects a `qemu` Git checkout and a specific upstream revision. `build_bionic.py` expects native source/build caches under `/opt/pocket2droid-network-integration`, a canonical cache under `/opt/pocket2droid-tcg-build`, and the toolchain under `/opt/pocket2droid-android-tools`. `build_slirp.py` has similar cache/toolchain assumptions. This snapshot provides the exact Android source tree and canonical changes, but no development `.git` history or ready-made `qemu` checkout.

A public build needs an explicit way to select the supplied Android tree or reconstruct the pinned canonical checkout, with configurable toolchain/cache locations.

### Shared helper paths and baseline checks

`android/build.py` imports helpers from a historical sibling source folder and checks a protected previous APK. The helpers are included under `android/build-tooling`, but the script does not yet load them from that location. Contributor builds need checkout-relative helper paths and independent validation, without requiring a previous maintainer APK.

### Signing

The current packaging script requires the maintainer's signing configuration and verifies the official signing certificate. That private key and password are excluded from this source snapshot.

Contributor builds need their own signing configuration. An APK signed with a different key cannot replace the official app as an ordinary update. Build testing should use a separate application ID to preserve official app data.

### Pinned development dependencies

The NDK and dependency records retain their hashes, but the setup recipe still relies on external caches and a live package index. A fresh setup needs the recorded dependencies, including development inputs beyond the bundled runtime packages, and an explicit source-verification step.

## Local checks

With the SDK path and host dependencies configured, the Android filesystem/settings suite is run from the source root with:

```sh
python3 android/tests/run_library_tests.py --settings
python3 android/tests/run_library_tests.py --settings --fallback
```

These checks use temporary local files and the host version of the JNI publication helper. They do not install an APK or operate on phone ROMs and saves. The runner currently expects `android.jar` at `/opt/pocket2droid-android-tools/android-36/android.jar`.

Recorded checks include 133 repository assertions, 98 settings/geometry assertions, 127 network configuration checks, 19 helper publication/profile checks, 13 session checks, seven guest-helper checks and 24 Android network UI/observer/socket checks with Engine ART lifecycle validation. Test10 also records 11 virtual-helper checks, 182 native tests and a disposable Windows Mobile autorun probe; the 36 Android StorageCard checks precede the cosmetic hint/icon changes. The hint and red vector were compiled and checked in the sealed APK. Final 1.0.2 retains the same behavioral payload as test10; these accepted checks are reused by exact source/payload identity. These are recorded release results, not a new run from this public checkout. They do not establish gameplay speed, full phone browsing or a fresh-checkout build.

The host network logic runner is `python3 android/tests/run_network_tests.py` with the same configured SDK and Java environment. Guest-helper checks are in `tests/test_net_setup.py`; native tests need a matching host engine and their documented fixtures. Do not run phone/probe scripts against personal ROMs or saves without reviewing their inputs.

The native clock correction has two calendar/timing regressions and 17 existing device checks. With a host QEMU built from this source, the clock tests can be run with:

```sh
P2D_QEMU=/path/to/host/qemu-system-arm python3 tests/test_rtc_clock.py
```

Historical validation: Big Range Hunting 2 startup was confirmed on Android using 1.0.1-test, whose runtime was retained in public v1.0.1. That result is not a new gameplay test of the test10 source.

## Version and release identity

- App name: `Pocket2Droid`
- Application ID: `com.samsixx.pocket2droid`
- Source/release version: `1.0.2`
- Internal Android version code: `682`

This is the source corresponding to the final signed 1.0.2 APK. It promotes test10 with version metadata changes only. The source update includes Windows Mobile 5, Restart, custom resolutions, networking, per-ROM StorageCard selection and a hidden virtual Internet setup helper. The exact Android native source, canonical changes, guest helper and dependency inputs are retained; only public documentation and Git ignore rules were refreshed for this GitHub preparation.

The internal version code continues from earlier development builds. Official updates retain the application ID and signing certificate and increase the code. Exit the running ROM normally before updating. A contributor key cannot replace the official app as an ordinary update; use the disposable test application ID for contributor testing.

`android/seal_v1_0_2.py` is the current archived maintainer release recipe. Earlier sealing scripts are historical. They require private workspace inputs and previous protected releases, which are intentionally not distributed here. They are not contributor build entry points.

The source ZIP contains a fresh `SOURCE_SHA256.json` inventory. Git tracks the source, notices and required recorded inputs, while excluding generated builds, private signing material and user data. Full phone browsing confirmation for the test10 existing-profile correction remains pending; no clean-machine build is claimed.
