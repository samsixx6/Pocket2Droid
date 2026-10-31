# Third-party components and references

Pocket2Droid uses QEMU for full-system emulation.

## QEMU

Upstream: https://www.qemu.org/

QEMU is distributed under GPLv2, with component-specific licenses.
Original copyright and license notices are retained in
`qemu-android-exact/`.

Pinned revisions and source provenance are recorded in
`sources.lock.json`.

## Android native dependencies

The native runtime includes GLib, PCRE2, GNU libiconv, pixman,
zlib and libandroid-support.

Dependency versions and hashes are recorded in
`android/build/bionic-tools.json` and `android/build/runtime.json`.
These components retain their respective licenses.

## libslirp

The network-enabled native engine statically links libslirp 4.9.5.
Upstream: https://gitlab.freedesktop.org/slirp/libslirp/-/tree/v4.9.5

The pinned source archive, complete upstream `COPYRIGHT`, changed `slirp.c`
and resolver patch are retained in `android/build/networking/android/source/`.
The provenance record in its parent directory records the archive, patch and
source hashes. `android/patches/LIBSLIRP-NOTICE.txt` describes the local change.
Upstream BSD-3-Clause and MIT notices remain intact.

## Guest network setup helper

`guest/network-setup/` contains Pocket2Droid's ARMv4 Windows CE helper and its
bounded PE builder. The exact packaged `autorun.exe` and hash manifest are in
`android/build/network-setup/production-helper/`. This executable is built from
the supplied source; it is not a Microsoft OS or SDK executable. No Windows
Mobile ROM, CE SDK or game is included.

## Microsoft Device Emulator references

Microsoft Device Emulator source was consulted for keyboard
scan codes and key sequences used for compatibility.

The downloaded reference source files, Microsoft emulator
executables and Windows Mobile ROM images are not bundled.

## License scope

The repository's main license does not replace third-party
copyright notices or component-specific licenses.

## Graphics bridge

The software GLES renderer includes PocketHLE pocket-gles under its supplied MIT license. Complete local source and LICENSE-MIT are retained under prototypes/gles-support/bridge-lab/host/vendor. Additional retained notices are in prototypes/gles-android/native/THIRD-PARTY-NOTICES.txt. The PVRTC decoder adaptation retains its MIT license and provenance in prototypes/gles-android/native/gpu. GPU/guest adapter code and ordinal data are included as source. No Windows Mobile ROM, game, vendor SDK DLL, signing key, or captured guest memory is included.
