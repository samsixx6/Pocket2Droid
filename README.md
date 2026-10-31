# Pocket2Droid

## Current release: 1.0.3.1

The source includes the GLES 1/GLES 2 GPU bridge, shared-memory graphics transport, newer compatibility fixes, and storage-preserving **Update/Change ROM** through test19. Gyro/tilt support is intentionally excluded. GLES setup is automatic and GPU preferred; the library shows a GLES badge for recognized built-in bridge ROMs. This source archive accompanies the signed 1.0.3.1 release APK. See BUILDING.md and CHANGELOG.md.

A Windows Mobile emulator for Android that runs the complete operating system, including compatible applications and games.

Pocket2Droid is under development; compatibility and performance vary by Android device, ROM and game.

## Download

Get the APK from the [Releases page](https://github.com/samsixx6/Pocket2Droid/releases).

## Join the official Pocket2Droid Discord for news, updates and support.

- [Discord](https://discord.gg/cVHYSwn9Bq)
- [YouTube](https://www.youtube.com/@Pocket2Droid)

## Screenshots

### Windows Mobile

| Windows Mobile 6 | Windows Mobile 6.5 | Windows Mobile 6.5 programs |
| :---: | :---: | :---: |
| <img src="screenshots/wm6%20Today%20screen.jpg" width="220" alt="Windows Mobile 6 Today screen with Pocket2Droid on-screen controls"> | <img src="screenshots/WM%206.5%20Today%20screen.jpg" width="220" alt="Windows Mobile 6.5 Today screen"> | <img src="screenshots/wm%206.5%20programs.jpg" width="220" alt="Windows Mobile 6.5 programs menu"> |

### Games

**Asphalt 4**

<img src="screenshots/Ashpalt%204.jpg" width="700" alt="Asphalt 4 gameplay in landscape">

| Dogs 2 | Need for Speed Undercover |
| :---: | :---: |
| <img src="screenshots/Dogs%202.jpg" width="300" alt="Dogs 2 gameplay"> | <img src="screenshots/Need%20for%20speed%20uc.jpg" width="300" alt="Need for Speed Undercover gameplay"> |

## Features

- ROM library with individual settings and persistent internal storage for each ROM
- Fixed shared storagecard folder and a file picker for copying files into it
- Guest resolution presets, screen fitting, portrait/landscape presentation and screen placement
- On-screen controls, adjustable transparency and vibration feedback
- button mapping for physical keyboards and wired/Bluetooth controllers
- Fullscreen mode and an optional display FPS overlay

The FPS overlay reports display presentations, not necessarily the game's actual frame rate.

## Requirements

- ARM64 Android device
- Android 7.0 or newer
- A compatible Windows Mobile `.bin` ROM image

ROMs, games and applications are not included. The emulated hardware is based on the Pocket PC Device Emulator platform; importing a manufacturer's ROM file does not guarantee that it will boot.

## Getting started

1. Install the APK from Releases and open **Pocket2Droid**.
2. Grant the requested storage access.
3. Tap **+**, then **PPC Rom**, to import a `.bin` ROM.
4. Tap the ROM in the library and wait for Windows Mobile to start.

Known ROM filenames select a resolution preset automatically. Check **ROM settings → Guest resolution** if the display looks incorrect.

## Adding applications and games

1. Exit the active ROM normally and return to the library.
2. Tap **+ → File to StorageCard** and choose your file, such as a CAB installer.
3. Start the ROM and open the copied file from its storage card inside Windows Mobile.

The file picker copies the selected file; it does not install it automatically. If the destination filename already exists, the copy receives a number suffix instead of replacing the existing file.

## Storage

The shared folder is `Internal storage/pocket2droid`:

| Folder | Contents |
| --- | --- |
| `Roms` | Imported ROM images |
| `data` | ROM profiles, library order and internal storage |
| `storagecard` | Files available to the guest operating system |

The guest operating system can modify files in its storagecard folder. Deleting a ROM from the library also deletes that ROM's internal storage, including installed applications, settings and saves; it leaves storagecard files intact.

## Reporting problems

Please [open an issue](https://github.com/samsixx6/Pocket2Droid/issues) with:

- Phone model and Android version
- Pocket2Droid version
- ROM filename
