# Isolated CE automatic GLES setup

Build inside WSL: `python3 prototypes/gles-android/guest/build.py` from the
qemu-devemu experiment. `--tests-only` runs portable C logic against real
temporary directories and recovered CoD2 executables. No real user files are
used by these tests. Final assets and SHA256 manifest are in `build/`.

The virtual `\\Storage Card\\P2DGLES` directory supplies `autorun.exe`,
`setup.exe`, `libGLES_CL.dll`, `libGLES_CM.dll`, `mode.txt` (`on` or `off`),
and optional `network.exe`. Composite autorun starts network only if its asset
exists, starts setup, closes child handles and returns immediately. A named
CE mutex prevents duplicate scanners.

Setup traverses Program Files and Storage Card once a minute, bounded to depth10,
512 directories,8192 entries,256 executable candidates and64MiB PE reads per
scan. Each PE is capped at8MiB; its exact-size buffer is freed before DLL
installation. P2DGLES,2577 and dot-prefixed entries are skipped. Trees beyond
these bounds can require manual setup. Files are admitted from ARM/Thumb CE
PE import tables, only when all CL/CM imports use known implemented names or
ordinals. DLLs disguised as EXEs, malformed images, unsupported GL APIs and
standalone libEGL/GLES DLL contracts are rejected. Dynamically loaded-only
GLES cannot be guaranteed. CoD2 software executable admits0, GLES admitsCL,
GoForce admitsCM. The separate Programs/Games shortcut targets cod2_gles.exe;
an existing unrelated shortcut is preserved. No executable/CAB/ROM/save is edited.

Each installed file has a112-byte `.p2d-gles-v1.owner` sidecar with format magic,
version, prior-presence bit, original/installed SHA256 and record checksum.
Different original DLLs are moved to a no-replace `.p2d-gles-v1.backup` before
new DLL publication. Marker creation is exclusive and flushed first. All copies
are exclusive, verified by SHA256; existing marker/backup collisions fail closed.
Matching preexisting bridge DLLs are not claimed. Off scans restore original
backups only when the installed file still matches the owned hash and backup
still matches its original hash. User edits and damaged/unrecognized sidecars
are preserved with a conflict result. Interrupted states before/after rename
can resume or restore. Files must not be edited concurrently during a helper
operation; CE does not provide atomic conditional hash-and-rename. The immutable
asset publisher and one-helper mutex prevent the app's own races.

CE CopyFile preserves the immutable source's read-only attribute. After the
new target's installed hash is verified, setup clears only its read-only bit;
original backup attributes stay intact. Off can repair this same bit on a
verified owned DLL from an earlier helper, then delete/restore. Changed bytes
are neither made writable nor removed. Native tests model CE CopyFile attribute
inheritance and DeleteFile read-only rejection on real temporary files.

`setup.log` is a small private per-scan summary (mode, entries, directories,
EXEs, supported games, applied files, conflicts, failures), overwritten each pass.
Bridge RPC filenames remain request.bin,request.ready,response.bin,guest.log.
`session.reset` is a4-byte little-endian nonzero CE process ID, written before
the first request in a new bridge process. Transport must reset native context
before that write completes. Single process/context is supported. The native
renderer is CPU software; general GLES conformance/Android FPS are not established.

Before touching logs/transport, bridge init claims a lifetime named CE kernel
mutex. An existing object makes the second game/context fail immediately,
without waiting, resetting the first renderer, or overwriting its logs. The
first handle stays live until DLL process detach/exit. Kernel process cleanup
also releases it on process death. Shared claim logic is tested with actual
separate native processes and a kernel object that disappears on handle close,
including abrupt owner death; actual CE kernel behavior remains a VM check.

`guest_bridge.pc-baseline.c` preserves the complete unchanged proven PC source.
`guest_bridge.c` adds exclusive process lifetime ownership, explicit Android
process reset and truthful renderer strings.
The separate canonical PC bridge and network helper remain untouched.
Final PE verification independently checks ARM/CE stamps, ordinal exports,
entry/export/import table alignment, coredll-only imports and manifest hashes.

CE FindData uses Microsoft's CE layout (`dwOID`, filename offset40), documented
in [WIN32_FIND_DATA, Windows CE5](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms892378%28v%3Dmsdn.10%29).
