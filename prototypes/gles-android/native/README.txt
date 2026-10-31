CURRENT SOURCE NOTE (2026-11-01)

This file records the earlier CPU-adapter prototype. The build.py/verify.py
commands and private artifacts below are historical, not supplied entry points.
For current C graphics libraries use tools/build_graphics.py from the
repository root as described in BUILDING.md. Build this Rust adapter with:
  cargo build --manifest-path prototypes/gles-android/native/Cargo.toml --release --locked
For Android, also select --target aarch64-linux-android and configure the NDK
linker and 16KiB linker alignment. Rename the resulting libp2d_gles.so to
libp2d_gles_cpu.so for deployment alongside the current selector. Its SONAME
must likewise be libp2d_gles_cpu.so (pass -C link-arg=-Wl,-soname,libp2d_gles_cpu.so
in the target Rust flags). The selector itself is deployed as libp2d_gles.so.

Historical prototype notes follow:

Pocket2Droid native PocketHLE GLES renderer

This wrapper embeds the existing bridge-lab/host adapter and its unchanged
PocketHLE pocket-gles crate. The renderer rasterizes on the CPU; it does not use
Android OpenGL/Vulkan or prove an Android gameplay performance gain.

Build from WSL Ubuntu-24.04 with Python3:
  python3 build.py rustup target add aarch64-linux-android
  python3 build.py --log evidence/abi-green.log test --locked --offline
  python3 build.py renderer-tests
  python3 build.py notices
  python3 build.py libraries
  python3 verify.py

Only the experiment's private bridge-lab/tools/{cargo,rustup} homes are used.
Build output is native/target, never the baseline caches or existing host/target.
Android uses NDK r30 API24 aarch64-linux-android24-clang and explicit 16KiB LOAD
alignment, with panic=unwind caught at the ABI. Host is x86_64 Linux.

Deliverable libraries:
  artifacts/host/libp2d_gles.so
  artifacts/android-arm64-v8a/libp2d_gles.so
Both expose exactly the three functions declared in libp2d_gles.h. Read that
header for lifetime/argument/status rules. Each engine session owns its context;
destroy/create is required for guest reset and after a caught renderer panic.
The library is dlopen-compatible; mode Off needs no load or context.

Android dependencies must be platform libraries only. verify.py checks ELF64,
target machine, exact exports, no writable/executable LOAD or executable stack,
Android SONAME, system-only dependencies and every Android LOAD alignment. It
also runs real host-library ABI tests via ctypes and a C program using the header.
These checks do not execute the Android binary on a device or establish full OS
integration, GLES conformance or gameplay speed.

Sources/license: https://github.com/j92580498-max/PocketHLE, PocketHLE Contributors.
The retained vendor crate describes Apache-2.0 OR MIT; distribution here uses
the supplied MIT license (LICENSE-PocketHLE-MIT). Bridge adapter source and its
upstream renderer/ordinal/license inputs are hashed before/after the build in
evidence/BUILD-PROVENANCE.json. No upstream or existing adapter file is modified.
External Rust registry dependency versions/checksums are pinned in Cargo.lock.
THIRD-PARTY-NOTICES.txt retains the supplied renderer license and license files
from the exact locked registry dependencies for inclusion in APK distribution.

The upstream optional POCKETHLE_DUMP_TEXTURES environment feature could write
outside private state. create() fails if it is set, without changing global
environment. Both the build/test harness and Android engine should leave it unset.
Arbitrary invalid non-null C addresses cannot be validated by Rust; native callers
must obey the documented accessible-buffer contract. Nulls, misalignment, bounds
and address overflow are rejected before request dereference.
